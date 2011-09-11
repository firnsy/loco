#include "common.h"
#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>

//
// CONTROL MESSAGES (TCP)
//

int send_control_message(int fd, uint32_t code, uint32_t value)
{
  uint32_t ctl_message = ((code & 0xff) << 24) | (value & 0xffffff);

  if ( fd < 0 )
    return 0;

  ulog(LOG_DEBUG, "[S>] M=%u C=%u V=%u\n", ctl_message, code, value);

  ctl_message = htonl(ctl_message);
  if (write(fd, (char *)&ctl_message, sizeof(uint32_t)) != sizeof(uint32_t))
    return 1;

  return 0;
}

int receive_control_message(int fd, uint32_t *code, uint32_t *value)
{
  uint32_t ctl_message = 0;
  uint32_t ctl_code = 0;
  uint32_t ctl_value = 0;
  int bytes_read = 0;

  // assume a fail
  int ret = 1;

  if ( fd < 0 )
    return 0;

  bytes_read = read(fd, (char *)&ctl_message, sizeof(uint32_t));

  if ( bytes_read == sizeof(uint32_t))
  {
    ctl_message = ntohl(ctl_message);
    ctl_code = (ctl_message & 0xff000000) >> 24;
    ctl_value = (ctl_message & 0x00ffffff);

    ret = 0;
  }
  else if ( bytes_read < 0 )
  {
    ret = 2;
    ulog(LOG_DEBUG, "errno: %d\n", errno);
  }
  else
  {
    ulog(LOG_DEBUG, "Bytes Read: %d %d\n", bytes_read, sizeof(uint32_t));
  }

  if ( bytes_read == 0 )
    ret = 2;

  if ( NULL != code )
    *code = (ctl_code);

  if ( NULL != value )
    *value = (ctl_value);

  ulog(LOG_DEBUG, "[R<] M=%u C=%u V=%u\n", ctl_message, ctl_code, ctl_value);

  return ret;
}


//
// TIME TOOLS
//
double time_delta_us(struct timeval t1, struct timeval t2)
{
  return (double)( (t2.tv_sec-t1.tv_sec)*1e6 + (t2.tv_usec-t1.tv_usec));
}


//
// ARRAY MANIPULATION
//

void array_sort(double array[], double array_ordered[], unsigned int elements)
{
  double array_temp[elements];
  int i, j;

  for (i=0; i<elements; i++)
    array_temp[i] = array[i];

  for (i=1; i<elements; i++)
    for (j=i-1; j>=0; j--)
    {
      if ( array_temp[j+1] < array_temp[j] )
      {
        array_ordered[j] = array_temp[j];
        array_temp[j] = array_temp[j+1];
        array_temp[j+1] = array_ordered[j];
      }
    }

  for (i=0; i<elements; i++)
    array_ordered[i] = array_temp[i];
}

void array_print(double array[], unsigned int elements)
{
  int i;

  printf("Array:\n");

  for (i=0; i<elements; i++)
    printf("  %u => %.4f\n", i, array[i]);
}


//
// ARRAY STATISTICS
//

double stat_array_interquartile_mean(double array[], unsigned int elements)
{
  int i;
  double array_sorted[elements];
  double array_total = 0.0;

  array_sort(array, array_sorted, elements);

  double quartile_observations = elements / 4;
  double quartile_range = elements / 2;
  int quartile_observations_truncated = (int)floor(quartile_observations);
  int quartile_observations_whole = quartile_observations_truncated + 1;

  // we want at least one whole observation
  if ( quartile_observations_whole < 1 )
    return -1;

  // add whole observations
  for ( i = quartile_observations_whole; i < elements - quartile_observations_whole; i++)
    array_total += array[i];

  // add truncated observations with weighting
  array_total += (array[quartile_observations_truncated] + array[elements - quartile_observations_truncated]) * ((double)quartile_observations_whole - quartile_observations);

  return array_total / quartile_range;
}

double stat_array_mean(double array[], unsigned int elements)
{
  int i;
  double array_total = 0.0;

  for (i=0; i<elements; i++)
    array_total += array[i];

  return array_total / (double)elements; 
}

double stat_array_std(double array[], unsigned int elements)
{
  int i;
  double array_total = 0.0;
  double array_mean = stat_array_mean(array, elements);

  for (i=0; i<elements; i++)
    array_total += pow(array[i] - array_mean, 2);

  return sqrt(array_total / (double)elements); 
}

double stat_array_median(double array[], unsigned int elements)
{
  int i,j;
  double array_sorted[elements];
  double swap;

  for (i=0; i<elements; i++)
    array_sorted[i] = array[i];

  for (i=0; i<elements; i++)
  {
    for (j=elements-1; j>i; j--)
    {
      if ( array_sorted[i] > array_sorted[i] )
      {
        swap = array_sorted[j];
        array_sorted[j] = array_sorted[j+1];
        array_sorted[j+1] = swap;
      }
    }
  }

  return array_sorted[(elements-(elements%2))/2];
}

double stat_array_kurtosis(double array[], unsigned int elements)
{
  int i;
  double array_total = 0.0;
  double array_variance = 0.0;
  double array_mean = stat_array_mean(array, elements);

  if ( elements < 3 )
    return -99999;

  for (i=0; i<elements; i++)
    array_total += pow(array[i] - array_mean, 2);

  array_variance = array_total / elements;

  if ( array_variance == 0 )
    return -99999;

  array_total = 0.0;
  for (i=0; i<elements; i++)
    array_total += pow(array[i] - array_mean, 4);

  return array_total / pow(array_variance, 2);
}



//
// MISC
//

int int_min(int a, int b)
{
  return (a < b) ? a : b;
}

int int_max(int a, int b)
{
  return (a > b) ? a : b;
}
