/**
 * Originally intended to use for gokarting this library offers a simple way to get basic lap timing information from a GPS based system.
 * This library does NOT interface with your GPS, simply feed it data and check the state.
 * Right now this only offers a single split time around the "start/finish" and would not work for many other purposes without modification.
 * 
 * The development of this library has been overseen, and all documentation has been generated using chatGPT4.
 */

#include "DovesLapTimer.h"

#ifdef DOVES_LAP_TIMER_DEBUG
  #define debugln Serial.println
  #define debug Serial.print
#else
  void laptimer_dummy_debug(...) {
  }
  #define debug laptimer_dummy_debug
  #define debugln laptimer_dummy_debug
#endif

#ifndef DOVES_LAP_TIMER_CROSSING_THRESHOLD_METERS
  #define DOVES_LAP_TIMER_CROSSING_THRESHOLD_METERS 10
#endif

DovesLapTimer::DovesLapTimer() {
  crossingThreshold = DOVES_LAP_TIMER_CROSSING_THRESHOLD_METERS;
}

double DovesLapTimer::paceDifference() {
  double currentLapDistance = currentLapOdometerStart == 0 || raceStarted == false ? 0 : totalDistanceTraveled - currentLapOdometerStart;
  unsigned long currentLapTime = millisecondsSinceMidnight - currentLapStartTime;

  if (currentLapDistance == 0 || bestLapDistance == 0) {
    // Avoid division by zero
    return 0.0;
  }

  // Calculate the pace for the current lap and the best lap
  double currentLapPace = currentLapTime / currentLapDistance;
  double bestLapPace = bestLapTime / bestLapDistance;

  // Calculate the pace difference
  double paceDiff = currentLapPace - bestLapPace;

  return paceDiff;  
}

// TODO: update function to be slightly more portable to allow for split timing
// currently the main "timing loop"
void DovesLapTimer::checkStartFinish(double currentLat, double currentLng) {
  double distToLine = INFINITY;
  if (isAcuteTriangle(currentLat, currentLng, startFinishPointALat, startFinishPointALng, startFinishPointBLat, startFinishPointBLng)) {
    distToLine = pointLineSegmentDistance(currentLat, currentLng, startFinishPointALat, startFinishPointALng, startFinishPointBLat, startFinishPointBLng);
  }

  if (crossing) {
    // Check if we've moved out of the threshold area
    if (distToLine > crossingThreshold) {
      debugln("probably crossed, lets calculate");
      crossing = false;

      // Interpolate the crossing point and its time
      double crossingLat, crossingLng, crossingOdometer;
      unsigned long crossingTime;
      #ifdef DOVES_LAP_TIMER_FORCE_LINEAR
      interpolateCrossingPoint(crossingLat, crossingLng, crossingTime, crossingOdometer, startFinishPointALat, startFinishPointALng, startFinishPointBLat, startFinishPointBLng);
      #else
      interpolateCrossingPointOnCurve(crossingLat, crossingLng, crossingTime, crossingOdometer, startFinishPointALat, startFinishPointALng, startFinishPointBLat, startFinishPointBLng);
      #endif

      debug("crossingLat: ");
      debugln(crossingLat, 6);
      debug("crossingLng: ");
      debugln(crossingLng, 6);
      debug("crossingOdometer: ");
      debugln(crossingOdometer);
      debug("crossingTime: ");
      debugln(crossingTime);

      if (raceStarted) {
        // increment lap counter
        laps++;
        // calculate lapTime
        unsigned long lapTime = crossingTime - currentLapStartTime;
        double lapDistance = crossingOdometer - currentLapOdometerStart;
        // Update the start time for the next lap
        currentLapStartTime = crossingTime;
        currentLapOdometerStart = crossingOdometer;

        // Process the lap time (e.g., display it, store it, etc.)
        debug("Lap Finish Time: ");
        debug(lapTime);
        debug(" : ");
        debugln((double)(lapTime/1000), 3);

        // log best and last time
        lastLapTime = lapTime;
        lastLapDistance = lapDistance;
        if(bestLapTime <= 0 || lastLapTime < bestLapTime) {
          bestLapTime = lastLapTime;
          bestLapDistance = lastLapDistance;
          bestLapNumber = laps;
        }
      } else {
        currentLapStartTime = crossingTime;
        currentLapOdometerStart = crossingOdometer;
        raceStarted = true;
        debugln("Race Started");
      }
      // Reset the crossingPointBuffer index and full status
      crossingPointBufferIndex = 0;
      crossingPointBufferFull = false;
      memset(crossingPointBuffer, 0, sizeof(crossingPointBuffer));
    } else {
      // Update the crossingPointBuffer with the current GPS fix
      crossingPointBuffer[crossingPointBufferIndex].lat = currentLat;
      crossingPointBuffer[crossingPointBufferIndex].lng = currentLng;
      crossingPointBuffer[crossingPointBufferIndex].time = millisecondsSinceMidnight;
      crossingPointBuffer[crossingPointBufferIndex].odometer = totalDistanceTraveled;
      crossingPointBuffer[crossingPointBufferIndex].speedKmh = currentSpeedkmh;

      crossingPointBufferIndex = (crossingPointBufferIndex + 1) % crossingPointBufferSize;
      if (crossingPointBufferIndex == 0) {
        crossingPointBufferFull = true;
      }

      debug("crossing = true, add to crossingPointBuffer: index[");
      debug(crossingPointBufferIndex);
      debug("] full[");
      debug(crossingPointBufferFull == true ? "True" : "False");
      debugln("]");
    }
  } else {
    if (distToLine < crossingThreshold) {
      debugln("we are possibly crossing");
      crossing = true;
    }
  }
}

// dont think were using this anymore... might be a useful util to leave in
double DovesLapTimer::angleBetweenVectors(double vector1X, double vector1Y, double vector2X, double vector2Y) {
  // Calculate the dot product of the two vectors
  double dotProduct = vector1X * vector2X + vector1Y * vector2Y;

  // Calculate the magnitudes of the two vectors
  double magnitudeVector1 = sqrt(vector1X * vector1X + vector1Y * vector1Y);
  double magnitudeVector2 = sqrt(vector2X * vector2X + vector2Y * vector2Y);

  // Calculate the cosine of the angle between the vectors
  double cosAngle = dotProduct / (magnitudeVector1 * magnitudeVector2);

  // To avoid potential floating-point errors, clamp the value of the cosine between -1 and 1
  cosAngle = max(min(cosAngle, 1.0), -1.0);

  // Calculate the angle in degrees using the arccosine function
  double angle = acos(cosAngle) * 180.0 / M_PI;

  return angle;
}

// were attempting to make sure the driver "can" actually cross the line and isnt off to the sides
bool DovesLapTimer::isAcuteTriangle(double lat1, double lon1, double lat2, double lon2, double lat3, double lon3) {
  // Calculate the side lengths using the haversine function
  double a = haversine(lat1, lon1, lat2, lon2);
  double b = haversine(lat1, lon1, lat3, lon3);
  double c = haversine(lat2, lon2, lat3, lon3);

  // Calculate the squares of the side lengths
  double aSquared = a * a;
  double bSquared = b * b;
  double cSquared = c * c;

  // Check if all angles are less than 90 degrees using the Pythagorean inequality
  return (aSquared + bSquared > cSquared) && (aSquared + cSquared > bSquared) && (bSquared + cSquared > aSquared);
}

// old method with some to be desired, isAcuteTriangle should work better
bool DovesLapTimer::isDriverNearLine(double driverLat, double driverLng, double pointALat, double pointALng, double pointBLat, double pointBLng) {
  // Calculate the distances between the driver and the line's endpoints
  double driverDistA = haversine(driverLat, driverLng, pointALat, pointALng);
  double driverDistB = haversine(driverLat, driverLng, pointBLat, pointBLng);
  double lineLength = haversine(pointALat, pointALng, pointBLat, pointBLng);

  // Check if both the distances from the driver to the endpoints are shorter than the length of the line
  return (driverDistA < lineLength) && (driverDistB < lineLength);
}

int DovesLapTimer::driverSideOfLine(double driverLat, double driverLng, double pointALat, double pointALng, double pointBLat, double pointBLng) {
  double lineDirectionX = pointBLat - pointALat;
  double lineDirectionY = pointBLng - pointALng;
  double driverToPointAX = driverLat - pointALat;
  double driverToPointAY = driverLng - pointALng;

  double crossProduct = lineDirectionX * driverToPointAY - lineDirectionY * driverToPointAX;

  if (crossProduct > 0) {
    return 1; // Driver is on one side of the line
  } else if (crossProduct < 0) {
    return -1; // Driver is on the other side of the line
  } else {
    return 0; // Driver is exactly on the line
  }
}

double DovesLapTimer::pointLineSegmentDistance(double pointX, double pointY, double startX, double startY, double endX, double endY) {
  double segmentLengthSquared = pow(endX - startX, 2) + pow(endY - startY, 2);

  if (segmentLengthSquared == 0) {
    // The line segment is actually a point
    return haversine(pointX, pointY, startX, startY);
  }

  double projectionScalar = ((pointX - startX) * (endX - startX) + (pointY - startY) * (endY - startY)) / segmentLengthSquared;

  double haversineStart = haversine(pointX, pointY, startX, startY);
  double haversineEnd = haversine(pointX, pointY, endX, endY);

  if (projectionScalar < 0.0) {
    // The projection of the point is outside the line segment, closest to the start point
    return haversineStart;
  } else if (projectionScalar > 1.0) {
    // The projection of the point is outside the line segment, closest to the end point
    return haversineEnd;
  }

  // The projection of the point is within the line segment
  double projectedX = startX + projectionScalar * (endX - startX);
  double projectedY = startY + projectionScalar * (endY - startY);
  return haversine(pointX, pointY, projectedX, projectedY);
}

double DovesLapTimer::haversine(double lat1, double lon1, double lat2, double lon2) {
  double radiusEarth = 6371000; // Earth's radius in meters

  // Convert latitude and longitude from degrees to radians
  double lat1Rad = radians(lat1);
  double lon1Rad = radians(lon1);
  double lat2Rad = radians(lat2);
  double lon2Rad = radians(lon2);

  // Calculate the differences in latitude and longitude
  double deltaLat = lat2Rad - lat1Rad;
  double deltaLon = lon2Rad - lon1Rad;

  // Calculate the Haversine formula components
  double a = pow(sin(deltaLat / 2), 2) + cos(lat1Rad) * cos(lat2Rad) * pow(sin(deltaLon / 2), 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));

  // Calculate the great-circle distance
  double distance = radiusEarth * c;
  return distance;
}

double DovesLapTimer::haversineAltitude(double prevLat, double prevLng, double prevAlt, double currentLat, double curentLng, double currentAlt) {
  double distWithAltitude = 0;
  if (prevLat != 0 && prevLng != 0) {
    double dist = haversine(prevLat, prevLng, currentLat, curentLng);
    double altDiff = currentAlt - prevAlt;
    distWithAltitude = sqrt(dist * dist + altDiff * altDiff);
  }
  return distWithAltitude;
}

/////////// private functions

double DovesLapTimer::interpolateWeight(double distA, double distB, float speedA, float speedB) {
  double weightedDistA = distA / speedA;
  double weightedDistB = distB / speedB;
  return weightedDistA / (weightedDistA + weightedDistB);
}

#ifdef DOVES_LAP_TIMER_FORCE_LINEAR
void DovesLapTimer::interpolateCrossingPoint(double& crossingLat, double& crossingLng, unsigned long& crossingTime, double& crossingOdometer, double pointALat, double pointALng, double pointBLat, double pointBLng) {
  int numPoints = crossingPointBufferFull ? crossingPointBufferSize : crossingPointBufferIndex;

  // Variables to store the best pair of points
  int bestIndexA = -1;
  int bestIndexB = -1;
  double bestSumDistances = DBL_MAX;

  // Iterate through the crossingPointBuffer, comparing the sum of distances from the start/finish line of each pair of consecutive points
  for (int i = 0; i < numPoints - 1; i++) {
    double distA = pointLineSegmentDistance(crossingPointBuffer[i].lat, crossingPointBuffer[i].lng, pointALat, pointALng, pointBLat, pointBLng);
    double distB = pointLineSegmentDistance(crossingPointBuffer[i + 1].lat, crossingPointBuffer[i + 1].lng, pointALat, pointALng, pointBLat, pointBLng);
    double sumDistances = distA + distB;

    int sideA = driverSideOfLine(crossingPointBuffer[i].lat, crossingPointBuffer[i].lng, pointALat, pointALng, pointBLat, pointBLng);
    int sideB = driverSideOfLine(crossingPointBuffer[i + 1].lat, crossingPointBuffer[i + 1].lng, pointALat, pointALng, pointBLat, pointBLng);

    debug("sideA: ");
    debug(sideA);
    debug(" sideB: ");
    debugln(sideB);

    // Update the best pair of points if the current pair has a smaller sum of distances and the points are on opposite sides of the line
    if (sumDistances < bestSumDistances && sideA != sideB ) {
      bestSumDistances = sumDistances;
      bestIndexA = i;
      bestIndexB = i + 1;

      // go ahead and exit as we have crossed the line
      break;
    }
  }

  // Interpolate the crossing point's latitude, longitude, and time using the best pair of points
  double distA = pointLineSegmentDistance(crossingPointBuffer[bestIndexA].lat, crossingPointBuffer[bestIndexA].lng, pointALat, pointALng, pointBLat, pointBLng);
  double distB = pointLineSegmentDistance(crossingPointBuffer[bestIndexB].lat, crossingPointBuffer[bestIndexB].lng, pointALat, pointALng, pointBLat, pointBLng);

  // Compute the interpolation factor based on distance and speed
  double t = interpolateWeight(distA, distB, crossingPointBuffer[bestIndexA].speedKmh, crossingPointBuffer[bestIndexB].speedKmh);
  crossingLat = crossingPointBuffer[bestIndexA].lat + t * (crossingPointBuffer[bestIndexB].lat - crossingPointBuffer[bestIndexA].lat);
  crossingLng = crossingPointBuffer[bestIndexA].lng + t * (crossingPointBuffer[bestIndexB].lng - crossingPointBuffer[bestIndexA].lng);
  crossingOdometer = crossingPointBuffer[bestIndexA].odometer + t * (crossingPointBuffer[bestIndexB].odometer - crossingPointBuffer[bestIndexA].odometer);
  crossingTime = crossingPointBuffer[bestIndexA].time + t * (crossingPointBuffer[bestIndexB].time - crossingPointBuffer[bestIndexA].time);
}
#else
double DovesLapTimer::catmullRom(double p0, double p1, double p2, double p3, double t) {
  // Calculate t^2 and t^3
  double t2 = t * t;
  double t3 = t2 * t;

  // Calculate the Catmull-Rom coefficients a, b, c, and d
  double a = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
  double b = p0 - 2.5 * p1 + 2 * p2 - 0.5 * p3;
  double c = -0.5 * p0 + 0.5 * p2;
  double d = p1;

  // Calculate and return the interpolated value using the coefficients and powers of t
  return a * t3 + b * t2 + c * t + d;
}

void DovesLapTimer::interpolateCrossingPointOnCurve(double& crossingLat, double& crossingLng, unsigned long& crossingTime, double& crossingOdometer, double pointALat, double pointALng, double pointBLat, double pointBLng) {
  int numPoints = crossingPointBufferFull ? crossingPointBufferSize : crossingPointBufferIndex;

  // Variables to store the best pair of points
  int bestIndexA = -1;
  int bestIndexB = -1;
  double bestSumDistances = DBL_MAX;

  // Iterate through the crossingPointBuffer, comparing the sum of distances from the start/finish line of each pair of consecutive points
  for (int i = 1; i < numPoints - 2; i++) {
    double distA = pointLineSegmentDistance(crossingPointBuffer[i].lat, crossingPointBuffer[i].lng, pointALat, pointALng, pointBLat, pointBLng);
    double distB = pointLineSegmentDistance(crossingPointBuffer[i + 1].lat, crossingPointBuffer[i + 1].lng, pointALat, pointALng, pointBLat, pointBLng);
    double sumDistances = distA + distB;

    int sideA = driverSideOfLine(crossingPointBuffer[i].lat, crossingPointBuffer[i].lng, pointALat, pointALng, pointBLat, pointBLng);
    int sideB = driverSideOfLine(crossingPointBuffer[i + 1].lat, crossingPointBuffer[i + 1].lng, pointALat, pointALng, pointBLat, pointBLng);

    debug("sideA: ");
    debug(sideA);
    debug(" sideB: ");
    debugln(sideB);

    // Update the best pair of points if the current pair has a smaller sum of distances and the points are on opposite sides of the line
    if (sumDistances < bestSumDistances && sideA != sideB) {
      bestSumDistances = sumDistances;
      bestIndexA = i;
      bestIndexB = i + 1;

      // go ahead and exit as we have crossed the line
      break;
    }
  }

  // Make sure we found a valid pair of points
  if (bestIndexA != -1 && bestIndexB != -1) {
    // Define the four control points for Catmull-Rom spline interpolation
    int index0 = bestIndexA - 1;
    int index1 = bestIndexA;
    int index2 = bestIndexB;
    int index3 = bestIndexB + 1;

    // Compute the interpolation factor based on distance
    double distA = pointLineSegmentDistance(crossingPointBuffer[index1].lat, crossingPointBuffer[index1].lng, pointALat, pointALng, pointBLat, pointBLng);
    double distB = pointLineSegmentDistance(crossingPointBuffer[index2].lat, crossingPointBuffer[index2].lng, pointALat, pointALng, pointBLat, pointBLng);
    double t = interpolateWeight(distA, distB, crossingPointBuffer[index1].speedKmh, crossingPointBuffer[index2].speedKmh);

    // Perform Catmull-Rom spline interpolation for latitude, longitude, time, and odometer
    crossingLat = catmullRom(crossingPointBuffer[index0].lat, crossingPointBuffer[index1].lat, crossingPointBuffer[index2].lat, crossingPointBuffer[index3].lat, t);
    crossingLng = catmullRom(crossingPointBuffer[index0].lng, crossingPointBuffer[index1].lng, crossingPointBuffer[index2].lng, crossingPointBuffer[index3].lng, t);
    crossingTime = catmullRom(crossingPointBuffer[index0].time, crossingPointBuffer[index1].time, crossingPointBuffer[index2].time, crossingPointBuffer[index3].time, t);
    crossingOdometer = catmullRom(crossingPointBuffer[index0].odometer, crossingPointBuffer[index1].odometer, crossingPointBuffer[index2].odometer, crossingPointBuffer[index3].odometer, t);
  }
}
#endif

/////////// getters and setters

void DovesLapTimer::reset() {
  // reset main race parameters
  raceStarted = false;
  currentLapStartTime = 0;
  lastLapTime = 0;
  bestLapTime = 0;
  currentLapOdometerStart = 0.0 ;
  lastLapDistance = 0.0 ;
  bestLapDistance = 0.0 ;
  bestLapNumber = 0;
  laps = 0;

  // reset odometer?
  totalDistanceTraveled = 0;
  posistionPrevLat = 0;
  posistionPrevLng = 0;
  posistionPrevAlt = 0;
  
  // Reset the crossingPointBuffer index and full status
  crossing = false;
  crossingPointBufferIndex = 0;
  crossingPointBufferFull = false;
  memset(crossingPointBuffer, 0, sizeof(crossingPointBuffer));
}
void DovesLapTimer::setStartFinishLine(double pointALat, double pointALng, double pointBLat, double pointBLng) {
  startFinishPointALat = pointALat;
  startFinishPointALng = pointALng;
  startFinishPointBLat = pointBLat;
  startFinishPointBLng = pointBLng;
}
void DovesLapTimer::updateOdometer(double currentLat, double currentLng, double currentAltitudeMeters) {
  totalDistanceTraveled += haversineAltitude(
    posistionPrevLat,
    posistionPrevLng,
    posistionPrevAlt,
    currentLat,
    currentLng,
    currentAltitudeMeters
  );
  posistionPrevLat = currentLat;
  posistionPrevLng = currentLng;
  posistionPrevAlt = currentAltitudeMeters;
}
void DovesLapTimer::updateCurrentTime(unsigned long currentTimeMilliseconds) {
  millisecondsSinceMidnight = currentTimeMilliseconds;
}
void DovesLapTimer::updateCurrentSpeedKmh(float currentSpeedkmh) {
  this->currentSpeedkmh = currentSpeedkmh;
}
bool DovesLapTimer::getRaceStarted() const {
  return raceStarted;
}
bool DovesLapTimer::getCrossing() const {
  return crossing;
}
unsigned long DovesLapTimer::getCurrentLapStartTime() const {
  return currentLapStartTime;
}
unsigned long DovesLapTimer::getCurrentLapTime() const {
  // todo: midnight rollover??? maybe convert to unixStamp?
  return currentLapStartTime <= 0 || raceStarted == false ? 0 : millisecondsSinceMidnight - currentLapStartTime;
}
unsigned long DovesLapTimer::getLastLapTime() const {
  return lastLapTime;
}
unsigned long DovesLapTimer::getBestLapTime() const {
  return bestLapTime;
}
float DovesLapTimer::getCurrentLapOdometerStart() const {
  return currentLapOdometerStart;
}
float DovesLapTimer::getCurrentLapDistance() const {
  return currentLapOdometerStart == 0 || raceStarted == false ? 0 : totalDistanceTraveled - currentLapOdometerStart;
}
float DovesLapTimer::getLastLapDistance() const {
  return lastLapDistance;
}
float DovesLapTimer::getBestLapDistance() const {
  return bestLapDistance;
}
float DovesLapTimer::getTotalDistanceTraveled() const {
  return totalDistanceTraveled;
}
int DovesLapTimer::getBestLapNumber() const {
  return bestLapNumber;
}
int DovesLapTimer::getLaps() const {
  return laps;
}
