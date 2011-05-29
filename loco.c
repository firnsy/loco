#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <signal.h>
#include <pthread.h>
#include <errno.h>

#include <fcntl.h>

#include <netdb.h> 

#include "common.h"
#include "debug.h"

struct mode_s
{
  int count;
  double lo;
  double hi;

  int bell_count;
  double bell_lo;
  double bell_hi;

  double bell_kurtosis;
};

struct config_s
{
  int udp_socket;
  int udp_port;
  struct sockaddr_in udp_addr;

  int tcp_socket;
  int tcp_port;
  struct sockaddr_in tcp_addr;

  char *hostname;
  struct hostent *server;

  int mode;
  char *csv_filepath;

  double rtt_tcp_socket_average;
  double latency_udp_kernel_user_average;

  double train_spacing;
  double train_spacing_min;
  double train_spacing_max;

  int train_length;
  int train_length_min;
  int train_length_max;

  int train_packet_length;
  int train_packet_length_min;
  int train_packet_length_max;

  double packet_dispersion_delta_min;

  // prelim

  double prelim_bw_mean;
  double prelim_bw_std;
   
  // phase 1 

  int p1_train_packet_length;
  int p1_train_packet_length_min;
  int p1_train_packet_length_max;

  double p1_trains_bw[4096];
  double p1_trains_delta[4096];
  int p1_trains_count;
  int p1_trains_count_discarded;

  struct mode_s p1_modes[1024];
  int p1_modes_count;

  // phase 2

  int p2_train_packet_length;
  int p2_train_packet_length_min;
  int p2_train_packet_length_max;

  double p2_trains_bw[4096];
  double p2_trains_delta[4096];
  int p2_trains_count;
  int p2_trains_count_discarded;

  struct mode_s p2_modes[1024];
  int p2_modes_count;

  double bandwidth_estimated;
  double bin_width;
};

int fsm_state = FSM_INIT;
struct config_s conf;


// private functions
int parse_cmdline(int argc, char **);
int init(void);
int init_packet_train(void);
char * create_packet_train(uint32_t train_id, uint32_t packet_id, int packet_length);
void exit_clean(int exit_code);
int fsm_state_get(void);
void fsm_state_set(int state);


int session_csv_read(const char *filepath);
int session_csv_write(const char *filepath);

int session_init(void);

int session_net_init(void);
int session_rtt_sync(void);
int session_prelim(void);
int session_p1(void);
int session_p1_calculate(void);
int session_p2(void);
int session_p2_calculate(void);

void session_calculate(void);

void session_end(void);

int receive_train(uint32_t train_id, int length, int packet_length, struct timeval *timestamps);

int calculate_mode(double ordered_array[], short validity_array[], int elements, double bin_width, struct mode_s *mode);

int main(int argc, char **argv)
{
  /* check command line arguments */
  if ( parse_cmdline(argc, argv) != 0 )
    exit_clean(1);

  if ( session_init() != 0 )
    exit_clean(1);

  //
  // CALCULATION SESSION

  if ( conf.mode == MODE_NET )
  {
    if ( session_net_init() != 0 )
      exit_clean(1);

    if ( session_rtt_sync() != 0 )
      exit_clean(1);

    if ( session_prelim() != 0 )
      exit_clean(1);

    if ( session_p1() != 0 )
      exit_clean(1);

    if ( session_p1_calculate() != 0 )
      exit_clean(1);

    if ( session_p2() != 0 )
      exit_clean(1);

    if ( session_p2_calculate() != 0 )
      exit_clean(1);
  }
  else
  {
    if ( session_csv_read(conf.csv_filepath) != 0 )
      exit_clean(1);

    // TODO: remove when saved to file
    conf.prelim_bw_mean = stat_array_mean(conf.p1_trains_bw, conf.p1_trains_count);

    fsm_state_set(FSM_P1_CALC);

    if ( session_p1_calculate() != 0 )
      exit_clean(1);

    fsm_state_set(FSM_P2_CALC);

    if ( session_p2_calculate() != 0 )
      exit_clean(1);
  }

  session_calculate();

  session_end();

  return 0;
}

//
// PRIVATE
//

void fsm_state_set(int state)
{
  if ( fsm_state == state )
    return;

  ulog(LOG_DEBUG, "Changing state %d => %d\n", fsm_state, state);

  fsm_state = state;
}

int fsm_state_get()
{
  return fsm_state;
}

int parse_cmdline(int argc, char **argv)
{
  extern char *optarg;
  int c;

  // set sane defaults
  conf.mode = MODE_NET;
  conf.csv_filepath = NULL;
  conf.hostname = NULL;
  conf.tcp_port = DEFAULT_TCP_SERVER_PORT;
  conf.udp_port = DEFAULT_UDP_CLIENT_PORT;

  while( (c=getopt(argc, argv, "h:Qr:w:")) != EOF )
  {
    switch (c)
    {
      case 'h':
        if ( NULL == conf.hostname )
          conf.hostname = strdup(optarg);
        break;
      case 'Q':
        break;
      case 'r':
        if ( NULL == conf.csv_filepath )
          conf.csv_filepath = strdup(optarg);

        conf.mode = MODE_CSV;
        break;
      case 'w':
        if ( NULL == conf.csv_filepath )
          conf.csv_filepath = strdup(optarg);
        break;
    }
  }



  return 0;
}

int session_init()
{
  // only valid if we're initialising
  if ( fsm_state_get() != FSM_INIT )
    return 1;

  // initialise calculated variables
  conf.train_length_min = TRAIN_LENGTH_MIN;
  conf.train_length_max = TRAIN_LENGTH_MAX;
  conf.p1_trains_count = 0;
  conf.p1_trains_count_discarded = 0;

  conf.p1_train_packet_length = TRAIN_PACKET_LENGTH_MIN;
  conf.p1_train_packet_length_min = TRAIN_PACKET_LENGTH_MIN;
  conf.p1_train_packet_length_max = TRAIN_PACKET_LENGTH_MAX;

  conf.p2_trains_count = 0;
  conf.p2_trains_count_discarded = 0;

  conf.p2_train_packet_length = TRAIN_PACKET_LENGTH_MIN;
  conf.p2_train_packet_length_min = TRAIN_PACKET_LENGTH_MIN;
  conf.p2_train_packet_length_max = TRAIN_PACKET_LENGTH_MAX;

  conf.packet_dispersion_delta_min = 0.0;

  conf.bandwidth_estimated = 0.0;
  conf.bin_width = 0.0;

  return 0;
}

int session_net_init()
{
  // only valid if we're initialising
  if ( fsm_state_get() != FSM_INIT )
    return 1;

  /* gethostbyname: get the server's DNS entry */
  conf.server = gethostbyname(conf.hostname);
  if (conf.server == NULL)
  {
    fprintf(stderr,"ERROR, no such host as %s\n", conf.hostname);
    exit_clean(1);
  }

  //
  // TCP SOCKET INIT

  conf.tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (conf.tcp_socket < 0) 
    exit_clean(1);

  /* build the server's Internet address */
  bzero((char *)&conf.tcp_addr, sizeof(conf.tcp_addr));
  conf.tcp_addr.sin_family = AF_INET;
  bcopy((char *)conf.server->h_addr, 
        (char *)&conf.tcp_addr.sin_addr.s_addr, conf.server->h_length);
  conf.tcp_addr.sin_port = htons(conf.tcp_port);

  if ( connect(conf.tcp_socket, (struct sockaddr *)&conf.tcp_addr, sizeof(conf.tcp_addr)) < 0 )
  { 
    ulog(LOG_FATAL, "Unable to connect on TCP socket.\n");
    exit_clean(1);
  }

  int tcp_flags = fcntl(conf.tcp_socket, F_GETFL, 0);
  fcntl(conf.tcp_socket, tcp_flags | O_NONBLOCK);

  // TCP SOCKET INIT - END
  //

  //
  // UDP SOCKET INIT

  conf.udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (conf.udp_socket < 0) 
    exit(1);

  /* build the server's Internet address */
  bzero((char *) &conf.udp_addr, sizeof(conf.udp_addr));
  conf.udp_addr.sin_family = AF_INET;
  conf.udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  conf.udp_addr.sin_port = htons(conf.udp_port);

  if ( bind(conf.udp_socket, (struct sockaddr *)&conf.udp_addr, sizeof(conf.udp_addr)) < 0 )
  {
    perror("bind(): ");
    exit(1);
  }

  int udp_flags = fcntl(conf.udp_socket, F_GETFL, 0);
  fcntl(conf.udp_socket, udp_flags | O_NONBLOCK);

  // UDP SOCKET INIT - END
  //



  send_control_message(conf.tcp_socket, MSG_SESSION_INIT, 0);

  // inform daemon our listening port for trains' destination
  send_control_message(conf.tcp_socket, MSG_SESSION_CLIENT_UDP_PORT_SET, conf.udp_port);

  fsm_state_set(FSM_RTT_SYNC);

  return 0;
}

int session_rtt_sync()
{
  // only valid if we're initialising
  if ( fsm_state_get() != FSM_RTT_SYNC )
    return 1;

  struct timeval t_mark1;
  struct timeval t_mark2;

  // calculate round trip time for TCP socket communications with daemon
  uint32_t ctl_code, ctl_value;

  int valid_count = 0;
  int count = 0;
  double rtt_total_time = 0;

  conf.rtt_tcp_socket_average = 0;

  while(valid_count < RTT_VALID_COUNT && count < RTT_COUNT_MAX)
  {
    gettimeofday(&t_mark1, (struct timezone*)0); 
    send_control_message(conf.tcp_socket, MSG_RTT_SYNC, count);
    receive_control_message(conf.tcp_socket, &ctl_code, &ctl_value);
    gettimeofday(&t_mark2, (struct timezone*)0); 

    if ( (count > 0) &&
         (ctl_value == (0xffffff-count)) )
    {
      rtt_total_time += time_delta_us(t_mark1, t_mark2);
      valid_count++;
    }

    count++;
  }

  if ( count == RTT_COUNT_MAX )
  {
    ulog(LOG_ERROR, "Unable to calculate RTT, too many failures.\n");
    return 1; 
  }

  // store calculated average
  conf.rtt_tcp_socket_average = (rtt_total_time / (double)RTT_VALID_COUNT);

  ulog(LOG_INFO, "Average round trip time (RTT): %.4fus\n", conf.rtt_tcp_socket_average);

  // provide sufficient room for the minimum train spacing base on the rtt
  if ( conf.train_spacing_min < conf.rtt_tcp_socket_average * 1.25 )
    conf.train_spacing_min = conf.rtt_tcp_socket_average * 1.25;

  send_control_message(conf.tcp_socket, MSG_TRAIN_SPACING_MIN_SET, conf.train_spacing_min);

  ulog(LOG_INFO, "Minimum train spacing: %.4fus\n", conf.train_spacing_min);

  // store maximum train spacing
  conf.train_spacing_max = conf.train_spacing_min * 2;

  send_control_message(conf.tcp_socket, MSG_TRAIN_SPACING_MAX_SET, conf.train_spacing_max);

  ulog(LOG_INFO, "Maximum train spacing: %.4fus\n", conf.train_spacing_max);

  // determine maximum packet size (base on TCP MSS)
  socklen_t opt_len;
  opt_len = sizeof(conf.train_packet_length_max);

  getsockopt(conf.tcp_socket, IPPROTO_TCP, TCP_MAXSEG, (char *)&conf.train_packet_length_max, &opt_len);
  conf.train_packet_length_max = (conf.train_packet_length_max > TRAIN_PACKET_LENGTH_MAX) ? TRAIN_PACKET_LENGTH_MAX : conf.train_packet_length_max;

  conf.p1_train_packet_length_max = conf.train_packet_length_max;
  conf.p2_train_packet_length_max = conf.train_packet_length_max;

  conf.train_packet_length_min = TRAIN_PACKET_LENGTH_MIN;

  ulog(LOG_INFO, "Minimum train packet length: %d bytes\n", conf.train_packet_length_min);
  ulog(LOG_INFO, "Maximum train packet length: %d bytes\n", conf.train_packet_length_max);

  // calculate latency of UDP packet transition from kernel to user space
  //
  // provides a value for the minimal possible delta for packet dispersions
  //
  // since the udp channel is used for the primary measurement criteria
  // we need to remove the average user/kernel latency from our final
  // measurement.
  //
 
  ulog(LOG_INFO, "[I] UDP kernel/userspace latency detection ...\n");

  int n;
  char *packet_random;
  double packet_deltas[LATENCY_VALID_COUNT] = { 0.0 };
  double latency_total_time = 0;

  // build a random packet (minimise any compression influences)
  packet_random = malloc(conf.train_packet_length_max * sizeof(char));
  if ( NULL == packet_random )
  {
    ulog(LOG_ERROR, "Unable to calculate latency. too many failures.\n");
    return 1;
  }

  opt_len = sizeof(conf.udp_addr);

  count = 0;
  valid_count = 0;

  while ( valid_count < LATENCY_VALID_COUNT && count < LATENCY_COUNT_MAX )
  {
    gettimeofday(&t_mark1, (struct timezone*)0); 
    sendto(conf.udp_socket, packet_random, conf.train_packet_length_max, 0, (struct sockaddr *)&conf.udp_addr, sizeof(struct sockaddr_in));
    n = recvfrom(conf.udp_socket, packet_random, conf.train_packet_length_max, 0, (struct sockaddr *)&conf.udp_addr, &opt_len);
    gettimeofday(&t_mark2, (struct timezone*)0); 

    if ( (count > 0) &&
         (n == conf.train_packet_length_max) )
    {
      packet_deltas[valid_count] = time_delta_us(t_mark1, t_mark2);
      latency_total_time += packet_deltas[valid_count];
      valid_count++;
    }
  
    count++;
  }

  // use the median to avoid outliers
  // multiplicative factor of 3 is taken from the literature (TODO)
  conf.packet_dispersion_delta_min = stat_array_median(packet_deltas, LATENCY_VALID_COUNT) * .5;

  ulog(LOG_INFO, "Minimum acceptable packet dispersion interval: %.4fus\n", conf.packet_dispersion_delta_min);

  if ( count == LATENCY_COUNT_MAX )
  {
    ulog(LOG_ERROR, "Unable to build a random packet for latency tests.\n");
    return 1;
  }

  conf.latency_udp_kernel_user_average = (latency_total_time / (double)LATENCY_VALID_COUNT / 2.0);
  
  ulog(LOG_INFO, "Average UDP kernel/user latency: %.4fus\n", conf.latency_udp_kernel_user_average);



  // determine the maximum train length over the wire
  //
  ulog(LOG_INFO, "[I] Maximum train length discovery ...\n");
  conf.train_length = TRAIN_LENGTH_MIN;
  conf.train_packet_length = conf.train_packet_length_max;

  struct timeval timestamps[TRAIN_LENGTH_MAX];
  int train_id = 1;
  int train_state = 0;
  int train_fails[TRAIN_LENGTH_MAX] = { 0 };
  int path_overload = 0;
  int train_count = 0;

  double bandwidth = 0.0;
  double delta = 0.0;

  // set initial train conditions
  send_control_message(conf.tcp_socket, MSG_TRAIN_ID_SET, train_id);
  send_control_message(conf.tcp_socket, MSG_TRAIN_LENGTH_SET, conf.train_length);
  send_control_message(conf.tcp_socket, MSG_TRAIN_PACKET_LENGTH_SET, conf.train_packet_length);

  while ( (conf.train_length <= TRAIN_LENGTH_MAX) &&
          (path_overload == 0) )
  {
    train_state = receive_train(train_id, conf.train_length, conf.train_packet_length, timestamps);

    // track the train fails to determine if we're overloading the wire
    if ( train_state != 0 )
    {
      train_fails[conf.train_length]++;

      if ( train_fails[conf.train_length] > 4 )
      {
        // we've failed at least three times at this length, we known enough
        path_overload = 1;
      }
      else if ( train_fails[conf.train_length] > 1 )
      {
        // we've failed before (at least twice) so let's go back
        conf.train_length -= ((conf.train_length-1) < TRAIN_LENGTH_MIN) ? 0 : 1;
        send_control_message(conf.tcp_socket, MSG_TRAIN_LENGTH_SET, conf.train_length);
      }

      continue;
    }
    
    delta = time_delta_us(timestamps[0], timestamps[conf.train_length-1]);
    bandwidth = (double)((conf.train_packet_length_max << 3) * conf.train_length) / delta;

    if ( delta > conf.packet_dispersion_delta_min )
    {
      conf.p1_trains_delta[conf.p1_trains_count] = delta;
      conf.p1_trains_bw[conf.p1_trains_count] = bandwidth;
      conf.p1_trains_count++;
    }
    else
      conf.p1_trains_count_discarded++;
 
    ulog(LOG_DEBUG, "Sent train of length: %u packets\n"
                    "  Received state: %d\n"
                    "  Detected bandwith: %f Mbps\n", conf.train_length, train_state, bandwidth);

    send_control_message(conf.tcp_socket, MSG_TRAIN_ID_SET, ++train_id);
    send_control_message(conf.tcp_socket, MSG_TRAIN_LENGTH_SET, ++conf.train_length);

    train_count++;
  }

  conf.train_length = TRAIN_LENGTH_MIN + 1;
  while ( train_fails[conf.train_length] < 3 && conf.train_length <= TRAIN_LENGTH_MAX )
  {
    conf.train_length++;
  }

  conf.train_length_max = conf.train_length-1;
  ulog(LOG_INFO, "Maximum train length: %d packets\n", conf.train_length_max);

  //
  // let's do a quick check to detect any interrupt coalescence
  // this will likely indicate a Gb+ link and we can bail early
  // 
  // 60% of measurements not stored
  if ( conf.p1_trains_count <= (int)((double)train_count * 0.4) )
  {
    ulog(LOG_DEBUG, "Average packet dispersion is less than the calculated packet dispersion minimum.\n"
                    "Assuming a Gb+ link.\n");

    conf.bandwidth_estimated = 1000.0;  
// TODO: remove when finished testing.
//    session_end();
  } 
    
  fsm_state_set(FSM_PRELIM);
  return 0;
}


int session_prelim()
{
  // only valid if we're initialising
  if ( fsm_state_get() != FSM_PRELIM )
    return 1;

  ulog(LOG_INFO, "[I] Preliminary assessment ...\n");

  struct timeval timestamps[TRAIN_LENGTH_MAX];

  int train_id = 1;
  int train_state = 0;
  int prelim_count = 0;
  int prelim_count_valid = 0;

  double delta = 0.0;
  double bandwidth = 0.0;

  conf.train_length = TRAIN_LENGTH_MIN;
  conf.train_packet_length = conf.train_packet_length_max;

  // set initial train conditions
  send_control_message(conf.tcp_socket, MSG_TRAIN_ID_SET, train_id);
  send_control_message(conf.tcp_socket, MSG_TRAIN_LENGTH_SET, conf.train_length);
  send_control_message(conf.tcp_socket, MSG_TRAIN_PACKET_LENGTH_SET, conf.train_packet_length);

  while (conf.train_length <= conf.train_length_max)
  {
    prelim_count = 0;
    prelim_count_valid = 0;

    while (prelim_count_valid < PRELIM_VALID_COUNT && prelim_count < PRELIM_COUNT_MAX)
    {
      train_state = receive_train(train_id, conf.train_length, conf.train_packet_length, timestamps);

      prelim_count++;

      // track the train fails to determine if we're overloading the wire
      if ( train_state != 0 )
      {
        continue;
      }
    
      delta = time_delta_us(timestamps[0], timestamps[conf.train_length-1]);
      bandwidth = (double)((conf.train_packet_length_max << 3) * conf.train_length) / delta;

      if ( delta > conf.packet_dispersion_delta_min )
      {
        conf.p1_trains_delta[conf.p1_trains_count] = delta;
        conf.p1_trains_bw[conf.p1_trains_count] = bandwidth;
        conf.p1_trains_count++;

        prelim_count_valid++;
      }
      else
        conf.p1_trains_count_discarded++;
 
      ulog(LOG_DEBUG, "Sent train of length: %u packets\n"
                      "  Detected bandwith: %f Mbps\n", conf.train_length, bandwidth);

      send_control_message(conf.tcp_socket, MSG_TRAIN_ID_SET, ++train_id);
    }

    conf.train_length++;
    send_control_message(conf.tcp_socket, MSG_TRAIN_LENGTH_SET, conf.train_length);
  }

  conf.prelim_bw_mean = stat_array_mean(conf.p1_trains_bw, conf.p1_trains_count);
  conf.prelim_bw_std = stat_array_std(conf.p1_trains_bw, conf.p1_trains_count);

  ulog(LOG_INFO, "Preliminary bandwidth measurements:\n"
                 "  Valid measurements: %d (out of %d)\n"
                 "  Average: %.4f Mbps\n"
                 "  Standard Deviation: %.4f Mbps\n"
                 "  Coefficient of Variance: %.4f\n", conf.p1_trains_count, conf.p1_trains_count + conf.p1_trains_count_discarded, conf.prelim_bw_mean, conf.prelim_bw_std, conf.prelim_bw_std/conf.prelim_bw_mean);

  if ( conf.prelim_bw_std/conf.prelim_bw_mean < BW_COVAR_THRESHOLD )
  {
    conf.bandwidth_estimated = conf.prelim_bw_mean;
    session_end();
  }

  if ( conf.prelim_bw_mean < 1.0 )
    conf.bin_width = conf.prelim_bw_mean * .25;
  else
    conf.bin_width = conf.prelim_bw_mean * .125;

  ulog(LOG_INFO, "Capacity resolution: %.4f\n", conf.bin_width);

  fsm_state_set(FSM_P1);

  return 0;
}


int session_p1()
{
  // only valid if we're initialising
  if ( fsm_state_get() != FSM_P1 )
    return 1;

  ulog(LOG_INFO, "[I] Phase 1 processing ...\n");

  struct timeval timestamps[TRAIN_LENGTH_MAX];

  int i;
  int train_id = 1;
  int train_state = 0;
  int p1_count = 0;
  int p1_count_valid = 0;
  int p1_train_count_required = 1000;

  int p1_packet_length_step = (int)((double)(conf.p1_train_packet_length_max - conf.p1_train_packet_length_min) / (double)TRAIN_PACKET_LENGTH_SIZES);

  int p1_train_count_size = (int)(p1_train_count_required / p1_packet_length_step);
  int p1_train_count_size_max = p1_train_count_size + P1_TRAIN_DISCARD_COUNT_MAX;

  double delta = 0.0;
  double bandwidth = 0.0;

  conf.train_length = TRAIN_LENGTH_MIN;
  conf.train_packet_length = conf.train_packet_length_min;

  for (i=0; i<TRAIN_PACKET_LENGTH_SIZES; i++)
  {

    // set initial train conditions
    send_control_message(conf.tcp_socket, MSG_TRAIN_ID_SET, train_id);
    send_control_message(conf.tcp_socket, MSG_TRAIN_LENGTH_SET, conf.train_length);
    send_control_message(conf.tcp_socket, MSG_TRAIN_PACKET_LENGTH_SET, conf.train_packet_length);

    ulog(LOG_INFO, "Train length: %d packets\n"
                    "Packet length: %d bytes\n"
                    "%d%% Complete\n", conf.train_length, conf.train_packet_length, (100 * i / TRAIN_PACKET_LENGTH_SIZES));

    p1_count = 0;
    p1_count_valid = 0;

    while (p1_count_valid < p1_train_count_size && p1_count < p1_train_count_size_max)
    {
      train_state = receive_train(train_id, conf.train_length, conf.train_packet_length, timestamps);

      p1_count++;

      // track the train fails to determine if we're overloading the wire
      if ( train_state != 0 )
      {
        continue;
      }
    
      delta = time_delta_us(timestamps[0], timestamps[conf.train_length-1]);
      bandwidth = (double)((conf.train_packet_length_max << 3) * conf.train_length) / delta;

      if ( delta > conf.packet_dispersion_delta_min )
      {
        conf.p1_trains_delta[conf.p1_trains_count] = delta;
        conf.p1_trains_bw[conf.p1_trains_count] = bandwidth;
        conf.p1_trains_count++;

        p1_count_valid++;
      }
      else
        conf.p1_trains_count_discarded++;
 
      ulog(LOG_DEBUG, "  Detected bandwith: %.4f Mbps (%.2f, %.2f)\n", bandwidth, delta, conf.packet_dispersion_delta_min);

      send_control_message(conf.tcp_socket, MSG_TRAIN_ID_SET, ++train_id);
    }

    // check if the maximum ignore threshold was hit
    if ( (p1_count - p1_count_valid) >= P1_TRAIN_DISCARD_COUNT_MAX )
    {
      // conf.train_length++;

      //if ( conf.train_length > int_max(conf.train_length_max / 4, 2) )
      if ( conf.train_length > conf.train_length_max )
      {
        ulog(LOG_DEBUG, "Giving up on %d %d %d\n", conf.train_length, conf.train_length_max, int_max(conf.train_length_max / 4, 2));
        break;
      }

      ulog(LOG_DEBUG, "Too many discarded trains, adjusting parameters.\n");
    }    
    else
    {
      // increment packet length
      conf.train_packet_length += p1_packet_length_step;
    }

    // clip our changes appropriately
    if ( conf.train_packet_length > conf.train_packet_length_max )
      conf.train_packet_length = conf.train_packet_length_max;
  }

  fsm_state_set(FSM_P1_CALC);

  return 0;
}

int session_p1_calculate()
{
  // only valid if we're initialising
  if ( fsm_state_get() != FSM_P1_CALC )
    return 1;

  ulog(LOG_INFO, "[I] Phase 1 mode calculation ...\n");

  if ( conf.prelim_bw_mean < 1.0 )
    conf.bin_width = conf.prelim_bw_mean * .25;
  else
    conf.bin_width = conf.prelim_bw_mean * .125;

  // sort the array in preperation for mode calculation and mark all trains as valid for classification
  array_sort(conf.p1_trains_bw, conf.p1_trains_bw, conf.p1_trains_count);

  short trains_valid[4096];
  int i;

  for (i=0; i<4096; i++)
    trains_valid[i] = 1;

  struct mode_s mode;

  while ( (i=calculate_mode(conf.p1_trains_bw, trains_valid, conf.p1_trains_count, conf.bin_width, &mode))  != -1 )
  {
    if ( i == 1)
      conf.p1_modes[conf.p1_modes_count++] = mode;
  }

  fsm_state_set(FSM_P2);

  return 0;
}

int session_p2()
{
  // only valid if we're initialising
  if ( fsm_state_get() != FSM_P2 )
    return 1;

  ulog(LOG_INFO, "[I] Phase 2 assessment ...\n");

  struct timeval timestamps[TRAIN_LENGTH_MAX];

  int train_id = 1;
  int train_state = 0;
  int p2_count = 0;
  int p2_count_valid = 0;
  int p2_train_count_required = 500;

  double delta = 0.0;
  double bandwidth = 0.0;

  conf.train_length = conf.train_length_max;
  conf.train_packet_length = conf.train_packet_length_max;

  // set initial train conditions
  send_control_message(conf.tcp_socket, MSG_TRAIN_ID_SET, train_id);
  send_control_message(conf.tcp_socket, MSG_TRAIN_LENGTH_SET, conf.train_length);
  send_control_message(conf.tcp_socket, MSG_TRAIN_PACKET_LENGTH_SET, conf.train_packet_length);

  while (p2_count_valid < p2_train_count_required)
  {
    train_state = receive_train(train_id, conf.train_length, conf.train_packet_length, timestamps);

    p2_count++;

    // track the train fails to determine if we're overloading the wire
    if ( train_state != 0 )
      continue;
    
    delta = time_delta_us(timestamps[0], timestamps[conf.train_length-1]);
    bandwidth = (double)((conf.train_packet_length_max << 3) * conf.train_length) / delta;

    if ( delta > conf.packet_dispersion_delta_min )
    {
      conf.p2_trains_delta[conf.p2_trains_count] = delta;
      conf.p2_trains_bw[conf.p2_trains_count] = bandwidth;
      conf.p2_trains_count++;

      p2_count_valid++;
    }
    else
      conf.p2_trains_count_discarded++;
 
    ulog(LOG_DEBUG, "Sent train of length: %u packets\n"
                    "  Detected bandwith: %f Mbps\n", conf.train_length, bandwidth);

    send_control_message(conf.tcp_socket, MSG_TRAIN_ID_SET, ++train_id);
  }

  fsm_state_set(FSM_P2_CALC);
  
  return 0;
}

int session_p2_calculate()
{
  // only valid if we're initialising
  if ( fsm_state_get() != FSM_P2_CALC )
    return 1;

  ulog(LOG_INFO, "[I] Phase 2 mode calculation ...\n");

  // sort the array in preperation for mode calculation and mark all trains as valid for classification
  array_sort(conf.p2_trains_bw, conf.p2_trains_bw, conf.p2_trains_count);

  short trains_valid[4096];
  int i;

  for (i=0; i<4096; i++)
    trains_valid[i] = 1;

  struct mode_s mode;

  while ( (i=calculate_mode(conf.p2_trains_bw, trains_valid, conf.p2_trains_count, conf.bin_width, &mode))  != -1 )
  {
    if ( i == 1 )
      conf.p2_modes[conf.p2_modes_count++] = mode;
  }

  fsm_state_set(FSM_CALC);

  return 0;
}

void session_calculate()
{
  //
  // calculate the average dispersion rate (ADR) from phase 2
  double adr = stat_array_mean(conf.p2_trains_bw, conf.p2_trains_count);
  double adr_std = stat_array_std(conf.p2_trains_bw, conf.p2_trains_count);

  ulog(LOG_INFO, "Final bandwidth measurements:\n"
                 "  Average Dispersion Rate: %.4f Mbps\n"
                 "  Standard Deviation: %.4f Mbps\n"
                 "  Coefficient of Variance: %.4f\n", adr, adr_std, adr_std/adr);

  if ( conf.p2_modes_count == 1 &&
       adr_std/adr < BW_COVAR_THRESHOLD && 
       adr / conf.prelim_bw_mean < ADR_THRESHOLD )
  {
    adr = (conf.p2_modes[0].hi + conf.p2_modes[1].lo) / 2;
  }
  else if ( conf.p2_modes_count > 1 )
  {
    ulog(LOG_INFO, "Phase 2 did not lead to a uni-modal distribution. Seriously guessing from here.\n");

    double merit = 0.0;
    double merit_max = 0.0;
    int i;
    int merit_max_index = 0;
    
    for (i=0; i<conf.p2_modes_count; i++)
    {
       merit = conf.p2_modes[i].bell_kurtosis * ((double)conf.p2_modes[i].count / (double)conf.p2_trains_count);
       if ( merit > merit_max )
       {
         merit_max = merit;
         merit_max_index = i;
       }
    }

    // calulate the ADR based on the mode with the largest kurtosis density
    adr = (conf.p2_modes[merit_max_index].hi + conf.p2_modes[merit_max_index].lo) / 2;
  }

  // if phase 1 completed
  if ( 1 )
  {
    double merit = 0.0;
    double merit_max = 0.0;
    int i;
    int merit_max_index = 0;
    
    for (i=0; i<conf.p1_modes_count; i++)
    {
      if ( conf.p1_modes[i].hi > adr )
      {
         merit = conf.p1_modes[i].bell_kurtosis * ((double)conf.p1_modes[i].count / (double)conf.p1_trains_count);

         if ( merit > merit_max )
         {
           merit_max = merit;
           merit_max_index = i;
         }
      }
    }

    if ( merit_max > 0.0 )
    {
      ulog(LOG_INFO, "Best guess mode:\n"
                     "  Count: %d (%d)\n"
                     "  Range: %.4f (%.4f) <=> %.4f (%.4f)\n"
                     "  Kurtosis: %.4f\n"
                     "  Merit: %.4f\n", 
                     conf.p1_modes[merit_max_index].count, conf.p1_modes[merit_max_index].bell_count,
                     conf.p1_modes[merit_max_index].lo, conf.p1_modes[merit_max_index].bell_lo,
                     conf.p1_modes[merit_max_index].hi, conf.p1_modes[merit_max_index].bell_hi, 
                     conf.p1_modes[merit_max_index].bell_kurtosis, merit_max);

      conf.bandwidth_estimated = (conf.p1_modes[merit_max_index].lo + conf.p1_modes[merit_max_index].hi) / 2;
    }
    else
      conf.bandwidth_estimated = adr;
  }
  else
  {
    ulog(LOG_INFO, "Phase 1 did not complete, the following estimate is the lower bound of the path.");
    conf.bandwidth_estimated = adr;
  }


}

void session_end()
{
//  fsm_state_set(FSM_CALC);
 
  // CSV line of the form
  // bandwidth (est), bin_width, packet pair dispersion minimum, UDP kernel/user space latency
  fprintf(stdout, "%.4f,%.4f,%.4f,%.4f\n", conf.bandwidth_estimated, conf.bin_width, conf.packet_dispersion_delta_min, conf.latency_udp_kernel_user_average);

  if ( conf.mode == MODE_NET )
    session_csv_write(conf.csv_filepath);

  exit_clean(0);
}

int session_csv_write(const char *filepath)
{
  // write to file
  int i;
  FILE *fp;

  if ( (fp=fopen("/tmp/test.csv", "w")) == NULL )
    return 1;

  //
  // dump phase 1 results
 
  // dump the total count we have
  fprintf(fp, "%d\n", conf.p1_trains_count);

  for (i=0; i<conf.p1_trains_count; i++)
    fprintf(fp, "%.4f,%.4f\n", conf.p1_trains_bw[i], conf.p1_trains_delta[i]);

  //
  // dump phase 2 results
 
  // dump the total count we have
  fprintf(fp, "%d\n", conf.p2_trains_count);

  for (i=0; i<conf.p2_trains_count; i++)
    fprintf(fp, "%.4f,%.4f\n", conf.p2_trains_bw[i], conf.p2_trains_delta[i]);

  fclose(fp);

  return 0;
}

int session_csv_read(const char *filepath)
{
  int i;
  FILE *fp;

  if ( (fp=fopen(filepath, "r")) == NULL )
    return 1;

  //
  // read phase 1 results
 
  fscanf(fp, "%d", &conf.p1_trains_count);

  ulog(LOG_DEBUG, "Reading %d values ...\n", conf.p1_trains_count);

  for (i=0; i<conf.p1_trains_count; i++)
    fscanf(fp, "%lf,%lf", &conf.p1_trains_bw[i], &conf.p1_trains_delta[i]);

  //
  // read phase 2 results
 
  fscanf(fp, "%d", &conf.p2_trains_count);

  ulog(LOG_DEBUG, "Reading %d values ...\n", conf.p2_trains_count);

  for (i=0; i<conf.p2_trains_count; i++)
    fscanf(fp, "%lf,%lf", &conf.p2_trains_bw[i], &conf.p2_trains_delta[i]);

  fclose(fp);

  return 0;
}

int receive_train(uint32_t train_id, int length, int packet_length, struct timeval *timestamps)
{
  struct timeval t_mark;
  struct timeval t_select;

  char packet_buffer[packet_length];

  int train_state = 0;
  int processing = 1;
  int train_sent = 0;
  int n = 0;
  
  uint32_t c_code, c_value;

  uint32_t expected_packet_id = 0;
  uint32_t received_packet_id = 0;
  uint32_t received_train_id = 0;

  fd_set read_fds;
  int max_fd;

  FD_ZERO(&read_fds);
  t_select.tv_sec = 0;
  t_select.tv_usec = 0;

  socklen_t opt_len = sizeof(conf.udp_addr);

  // grab the largest socket file descriptor for select
  max_fd = (conf.tcp_socket > conf.udp_socket) ? conf.tcp_socket : conf.udp_socket;


  FD_SET(conf.udp_socket, &read_fds);
  FD_SET(conf.tcp_socket, &read_fds);

  // ensure the TCP/UDP buffers are empty
  while ( select(max_fd + 1, &read_fds, NULL, NULL, &t_select) > 0)
  {
    if ( FD_ISSET(conf.udp_socket, &read_fds) )
    {
      n = recvfrom(conf.udp_socket, packet_buffer, packet_length, 0, (struct sockaddr *)&conf.udp_addr, &opt_len);
    }

    if ( FD_ISSET(conf.tcp_socket, &read_fds) )
    {
      receive_control_message(conf.tcp_socket, &c_code, &c_value); 
    }
      
    FD_SET(conf.udp_socket, &read_fds);
    FD_SET(conf.tcp_socket, &read_fds);
  }

  // send the train already
  send_control_message(conf.tcp_socket, MSG_TRAIN_SEND, train_id);

  int p;

  while ( processing )
  {
    t_select.tv_sec = 2;

    FD_SET(conf.udp_socket, &read_fds);
    FD_SET(conf.tcp_socket, &read_fds);

    if ( (p=select(max_fd + 1, &read_fds, NULL, NULL, &t_select)) == -1 )
    {
      perror("Select error: ");
      exit_clean(1);      
    } 

//    ulog(LOG_DEBUG, "select: %d\n", p);

    if ( FD_ISSET(conf.udp_socket, &read_fds) )
    {
      n=recvfrom(conf.udp_socket, packet_buffer, packet_length, 0, (struct sockaddr *)&conf.udp_addr, &opt_len);

      gettimeofday(&t_mark, (struct timezone *)0);

      memcpy(&received_train_id, packet_buffer, sizeof(uint32_t));
      memcpy(&received_packet_id, packet_buffer+sizeof(uint32_t), sizeof(uint32_t));
      received_train_id=ntohl(received_train_id);
      received_packet_id=ntohl(received_packet_id);

//      ulog(LOG_DEBUG, "Got train packet: %u (%u) %u (%u) %u\n", received_train_id, train_id, received_packet_id, expected_packet_id, length);

      if ( train_id != received_train_id )
      {
        train_state = 2;
      }
      else if (received_packet_id == expected_packet_id )
      {
        expected_packet_id++;

        // store the received timestamp since it's valid
        timestamps[received_packet_id] = t_mark;
      }

      if ( train_sent )
        if ( expected_packet_id == length )
          processing = 0;
    }

    // we've timed out or we have TCP data waiting
    // either are valid states to stop processing the train
    if ( FD_ISSET(conf.tcp_socket, &read_fds) )
    {
//      ulog(LOG_DEBUG, "Got end signal\n");
      receive_control_message(conf.tcp_socket, &c_code, &c_value); 

      if ( c_code == MSG_TRAIN_SENT )
      {
        train_sent = 1;

        if ( expected_packet_id == length )
          processing = 0;
      }
    }

    // timeout
    if ( p == 0 )
    {
      processing = 0;
    } 
  }

  if (expected_packet_id == length)
  {
    /* send control message to ACK burst */
    send_control_message(conf.tcp_socket, MSG_TRAIN_RECEIVE_ACK, 0);
  }
  else
  {
    /* send signal to recv_train */
    send_control_message(conf.tcp_socket, MSG_TRAIN_RECEIVE_FAIL, 0);
          
    train_state = 1;
  }


  return train_state;
}


int calculate_mode(double array_ordered[], short array_valid[], int elements, double bin_width, struct mode_s *mode)
{

  int i, j;
  int processing = 0;
  int count = 0;
  int mode_index_lo = 0;
  int mode_index_hi = 0;
  double mode_lo = 0.0;
  double mode_hi = 0.0;

  int bell_index_lo = 0;
  int bell_index_hi = 0;
//  double bell_lo = 0.0;
//  double bell_hi = 0.0;

  int bin_count = 0;
  int bin_index_lo = 0;
  int bin_index_hi = 0;
  double bin_lo = 0.0;
  double bin_hi = 0.0;

  int lbin_count = 0;
  int lbin_index_lo = 0;
  int lbin_index_hi = 0;

  int rbin_count = 0;
  int rbin_index_lo = 0;
  int rbin_index_hi = 0;

  int bin_count_tolerance = 0;

  ulog(LOG_DEBUG, "Checking train measurement validity.\n");

  // ensure we have trains left to classify
  j = 0;
  for (i=0; i<elements; i++)
    j += array_valid[i];

  // no trains level for classification
  if ( j == 0 )
      return -1;

  ulog(LOG_DEBUG, "Calculating modes...\n");
  ulog(LOG_DEBUG, "  Unclassified values: %d\n", j);

  // initialise the struct
  mode->count = 0; 
  mode->lo = 0; 
  mode->hi = 0; 
  mode->bell_count = 0; 
  mode->bell_lo = 0; 
  mode->bell_hi = 0; 
  mode->bell_kurtosis = 0; 

  //
  // find the bin of the primary mode from unclassified trains

  // find the window, of length bin_width, with the maximum number of consecutive values.
  count = 0;
  for (i=0; i<elements; i++)
  {
    if ( array_valid[i] )
    {
      j = i;
      while ( j < elements && array_valid[j] && array_ordered[j] < (array_ordered[i] + bin_width) )
        j++;

      if ( count < (j-i) )
      {
        count = j-i;
        mode_index_lo = i;
        mode_index_hi = j-1;
      }
    }
  }

  mode_lo = array_ordered[mode_index_lo];
  mode_hi = array_ordered[mode_index_hi];

  
  ulog(LOG_DEBUG, "  Central bin:\n"
                  "    Range: %.4f (%d) <=> %.4f (%d)\n"
                  "    Count: %d\n", mode_lo, mode_index_lo, mode_hi, mode_index_hi, count);

  mode->count = count;
  mode->lo = mode_lo;
  mode->hi = mode_hi;

  mode->bell_count = count;
  mode->bell_lo = mode_lo;
  mode->bell_hi = mode_hi;

  bell_index_lo = mode_index_lo; 
  bell_index_hi = mode_index_hi; 

  //
  // find all bins to the left of the primary that are part of the same modes bell.

  bin_count = mode->count;
  bin_count_tolerance = (int)(BIN_COUNT_TOLERANCE * (double)count);

  bin_index_lo = mode_index_lo;
  bin_index_hi = mode_index_hi;
  bin_lo = mode_lo;
  bin_hi = mode_hi;

  processing = 1;
  while ( processing )
  {
    // find window left of modal bin with (at most) bin_width with maximum number of measurements

    lbin_count = 0;
    if ( bin_index_lo > 0 )
    {
      for (i=bin_index_hi-1; i>bin_index_lo-1; i--)
      {
        count = 0;
        for (j=i; j>0; j--)
          if ( array_ordered[j] > (array_ordered[i]-bin_width) )
            count++;
          else
            break;

        if ( count > lbin_count )
        {
          lbin_count = count;
          lbin_index_lo = j+1;
          lbin_index_hi = i;
        }
      }
    }

    if ( lbin_count > 0 )
    {
      if ( lbin_count < bin_count + bin_count_tolerance )
      {
//        ulog(LOG_DEBUG, "    Inside MODE %d %d %d\n", i, lbin_count, bin_count_tolerance);

//        ulog(LOG_DEBUG, "  Left bin:\n"
//                        "    Range: %.4f (%d) <=> %.4f (%d)\n"
//                        "    Count: %d\n", array_ordered[lbin_index_lo], lbin_index_lo, array_ordered[lbin_index_hi], lbin_index_hi, lbin_count);

        mode->bell_count += (bin_index_lo-lbin_index_lo);
        bell_index_lo = lbin_index_lo;            
        mode->bell_lo = array_ordered[bell_index_lo];

        // reset counters for next iteration
        bin_count = lbin_count;
        bin_count_tolerance = (int)(BIN_COUNT_TOLERANCE * (double)lbin_count);

        bin_index_lo = lbin_index_lo;
        bin_index_hi = lbin_index_hi;
        bin_lo = array_ordered[bin_index_lo];
        bin_hi = array_ordered[bin_index_hi];
      }
      else
      {
        processing = 0;
//        ulog(LOG_DEBUG, "    Outside MODE\n");
      } 
    }
    else
      processing = 0;
  }

  //
  // find all bins to the left of the primary that are part of the same modes bell.

  bin_count = mode->count;

  bin_index_lo = mode_index_lo;
  bin_index_hi = mode_index_hi;
  bin_lo = mode_lo;
  bin_hi = mode_hi;

  processing = 1;
  while ( processing )
  {
    // find window left of modal bin with (at most) bin_width with maximum number of measurements

    rbin_count = 0;
    if ( bin_index_lo < elements - 1 )
    {
      for (i=bin_index_lo+1; i<bin_index_hi+1; i++)
      {
        count = 0;
        for (j=i; j<elements; j++)
          if ( array_ordered[j] <= (array_ordered[i]+bin_width))
            count++;
          else
            break;

        if ( count > rbin_count )
        {
          rbin_count = count;
          rbin_index_lo = i;
          rbin_index_hi = j-1;
        }
      }
    }

    if ( rbin_count > 0 )
    {
      if ( rbin_count < bin_count + bin_count_tolerance )
      {
//        ulog(LOG_DEBUG, "    Inside MODE\n");

//        ulog(LOG_DEBUG, "  Right bin:\n"
//                        "    Range: %.4f (%d) <=> %.4f (%d)\n"
//                        "    Count: %d\n", array_ordered[rbin_index_lo], rbin_index_lo, array_ordered[rbin_index_hi], rbin_index_hi, rbin_count);

        mode->bell_count += (rbin_index_hi-bin_index_hi);
        bell_index_hi = rbin_index_hi;
        mode->bell_hi = array_ordered[bell_index_hi];

        // reset counters for next iteration
        bin_count = rbin_count;
        bin_count_tolerance = (int)(BIN_COUNT_TOLERANCE * (double)rbin_count);

        bin_index_lo = rbin_index_lo;
        bin_index_hi = rbin_index_hi;
        bin_lo = array_ordered[bin_index_lo];
        bin_hi = array_ordered[bin_index_hi];
      }
      else
      {
        processing = 0;
//        ulog(LOG_DEBUG, "    Outside MODE\n");
      } 
    }
    else
      processing = 0;
  }

  // mark all values in our modal bell as classified
  for (i=bell_index_lo; i<=bell_index_hi; i++)
    array_valid[i] = 0;


  if ( mode->count > BIN_COUNT_NOISE_THRESHOLD )
  {

    // calculate kurtosis
    mode->bell_kurtosis = stat_array_kurtosis(array_ordered+bell_index_lo, bell_index_hi-bell_index_lo+1);
    if ( mode->bell_kurtosis == -99999 )
      return 0;

    // characteristics
    ulog(LOG_INFO , "  Mode:\n"
                    "    Count: %d\n"
                    "    Bell count: %d\n"
                    "    Range: %.4f (%d) <=> %.4f (%d)\n"
                    "    Kurtosis: %.4f\n", mode->count, mode->bell_count, array_ordered[bell_index_lo], bell_index_lo, array_ordered[bell_index_hi], bell_index_hi, mode->bell_kurtosis);


    return 1;
  }

  return 0;
}


void exit_clean(int exit_code)
{
  // tell daemon we're bailing out
  if ( fsm_state_get() != FSM_INIT && conf.mode == MODE_NET)
  {
    send_control_message(conf.tcp_socket, MSG_SESSION_END, (uint32_t)(exit_code & 0xffffffff));

    if ( conf.tcp_socket > 0 )
      close(conf.tcp_socket);
  }

  exit(exit_code);
}
