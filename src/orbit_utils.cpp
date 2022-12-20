#include "orbit_utils.h"

static int get_angle( const char *buff) {
   int rval = 0;

   while( *buff == ' ')
      buff++;
   while( *buff != ' ')
      {
      if( *buff != '.')
         rval = rval * 10 + (int)( *buff - '0');
      buff++;
      }
   return rval;
}

static double get_eight_places( const char *ptr) {
   return (double)atoi( ptr) + (double)atoi(ptr + 4) * 1e-8;
}

double getJulianFromUnix( long unixSecs ) {
   return ( unixSecs / SECONDS_PER_DAY ) + J2U;
}

long getUnixSecFromJulian(double julian) {
    return long((julian - J2U) * SECONDS_PER_DAY);
}

double getEraFromJulian(double julian) {
    double Tu = julian - 2451545.0;
    return fmod(TWO_PI * (0.7790572732640 + 1.00273781191135448 * Tu),TWO_PI);
}

long JulianDate(int year, int month, int day) {
	long JD_whole;
	int A, B;
	if (month <= 2) {
		year--;
		month += 12;
	}
	A = year / 100;
	B = 2 - A + A / 4;
	JD_whole = (long) (365.25 * (year + 4716)) + (int) (30.6001 * (month + 1))
			+ day + B - 1524;
	return JD_whole;
}

double eccAnomalyFromMean(double M0, double ecc) {
    double E = M0, dE = 1.;
    // Iterative Newton-Raphson solution for eccentic anomaly
    while (dE > 1e-8) {
        dE = (E - ecc*sin(E) - M0)/(1. - ecc*cos(E));
        E = E - dE;
    }
    return E;
}

double trueAnomalyFromEcc(double E, double ecc) {
    return 2*atan2(sqrt(1.+ecc)*sin(E/2.),sqrt(1.-ecc)*cos(E/2.));
}

double trueAnomalyFromMean(double M0, double ecc) {
    double E = eccAnomalyFromMean(M0,ecc);
    return trueAnomalyFromEcc(E,ecc);
}


void Orbit::initFromTLE(char* line1, char* line2) {
    char tbuff[13];

    int year = line1[19] - '0';
    if( line1[18] >= '0')
        year += (line1[18] - '0') * 10;
    if( year < 57)          /* cycle around Y2K */
        year += 100;
    epoch_J = get_eight_places( line1 + 20) + J1900
            + (double)( year * 365 + (year - 1) / 4);

    epochUTC = getUnixSecFromJulian(epoch_J);

    incl = (double)get_angle( line2 + 8) * (PI / 180e+4);
    Omega = (double)get_angle( line2 + 17) * (PI / 180e+4);
    ecc = atoi( line2 + 26) * 1.e-7;
    omega = (double)get_angle( line2 + 34) * (PI / 180e+4);
    M0 = (double)get_angle( line2 + 43) * (PI / 180e+4);

    /* Make sure mean motion is null-terminated, since rev. no.
    may immediately follow. */
    memcpy( tbuff, line2 + 51, 12);
    tbuff[12] = '\0';
    /* Input mean motion,  derivative of mean motion and second  */
    /* deriv of mean motion,  are all in revolutions and days.   */
    /* Convert them here to radians and seconds:                 */
    n = get_eight_places( tbuff) * TWO_PI / SECONDS_PER_DAY;
    n_dot = (double)atoi( line1 + 35)
                    * 1.0e-8 * TWO_PI / (SECONDS_PER_DAY*SECONDS_PER_DAY);
    if( line1[33] == '-')
        n_dot *= -1.;

    a = pow(MU_EARTH/(n*n),1./3.);
}

Vec3 Orbit::calcPosECI(double dt_sec) {
    double n_t = n + n_dot*dt_sec;

    double M_t = M0 + n_t*dt_sec;

    double E_t = eccAnomalyFromMean(M_t,ecc);
    // Get true anomaly
    double v_t = trueAnomalyFromEcc(E_t,ecc);
    // Get distance from center
    double r_c = a*(1-ecc*cos(E_t));

    double o_x = r_c*cos(v_t);
    double o_y = r_c*sin(v_t);

    double cos_w = cos(omega);
    double cos_O = cos(Omega);
    double cos_i = cos(incl);

    double sin_w = sin(omega);
    double sin_O = sin(Omega);
    double sin_i = sin(incl);

    Vec3 posECI;

    posECI.x = o_x*(cos_w*cos_O - sin_w*cos_i*sin_O)
                - o_y*(sin_w*cos_O + cos_w*cos_i*sin_O);
    posECI.y = o_x*(cos_w*sin_O + sin_w*cos_i*sin_O)
                - o_y*(cos_w*cos_i*cos_O - sin_w*sin_O);
    posECI.z = o_x*(sin_w*sin_i) + o_y*(cos_w*sin_i);

    return posECI;
}

void Orbit::calcPosVelECI(double dt_sec, Vec3& posECI, Vec3& velECI) {
    double n_t = n + n_dot*dt_sec;

    double M_t = M0 + n_t*dt_sec;

    double E_t = eccAnomalyFromMean(M_t,ecc);
    // Get true anomaly
    double v_t = trueAnomalyFromEcc(E_t,ecc);
    // Get distance from center
    double r_c = a*(1-ecc*cos(E_t));

    
    double cos_w = cos(omega);
    double cos_O = cos(Omega);
    double cos_i = cos(incl);

    double sin_w = sin(omega);
    double sin_O = sin(Omega);
    double sin_i = sin(incl);

    Dcm dcm_plane2ECI = {cos_w*cos_O - sin_w*cos_i*sin_O,
                            cos_w*sin_O + sin_w*cos_i*sin_O,
                            sin_w*sin_i,
                            -(sin_w*cos_O + cos_w*cos_i*sin_O),
                            (cos_w*cos_i*cos_O - sin_w*sin_O),
                            cos_w*sin_i,
                            0, 0, 0};

    double o_x = r_c*cos(v_t);
    double o_y = r_c*sin(v_t);

    double v = sqrt(MU_EARTH * a)/r_c;
    double v_x = v * -1.0*sin(E_t);
    double v_y = v * (sqrt(1.0 - (ecc*ecc)) * cos(E_t));

    Vec3 posPlane = {o_x,o_y,0};
    Vec3 velPlane = {v_x,v_y,0};

    posECI = dcm_plane2ECI * posPlane;
    velECI = dcm_plane2ECI * velPlane;
}

void Orbit::calcPosVelECI_UTC(long UTC, Vec3& posECI, Vec3& velECI) {
    double dt = double(UTC - epochUTC);
    calcPosVelECI(dt,posECI,velECI);
}
