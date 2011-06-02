#include "locod.h"

#include "common.h"
#include "debug.h"

#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <getopt.h>
// global variables

struct config_s
{
  // binding address information
  int udp_socket;
  int udp_port;
  struct sockaddr_in udp_addr;

  int tcp_socket;
  int tcp_port;
  struct sockaddr_in tcp_addr;

  // global variables
  char *random_packet;

  struct timeval time_now;

  int fsm_state;

  // session specific variables
  struct hostent *receiver;

  int failed_messages;

  // receiving address information
  struct sockaddr_in tcp_cli_addr;
  int tcp_fd;

  struct sockaddr_in udp_cli_addr;
  int udp_cli_port;

  // train description
  int train_id;

  double train_spacing;
  double train_spacing_min;
  double train_spacing_max;

  unsigned int train_length;
  unsigned int train_length_max;

  unsigned int train_packet_length;
  unsigned int train_packet_length_min;
  unsigned int train_packet_length_max;
};

struct config_s conf;


// private functions
int parse_cmdline(int argc, char **argv);
void banner(void);
void usage(const char *);
int init(void);
int init_packet_train(void);
char * create_packet_train(uint32_t train_id, uint32_t packet_id, unsigned int packet_length);
int send_train(uint32_t id, unsigned int length, unsigned int packet_length, const struct sockaddr_in * client_address);
void signal_handler(int signal);
int exit_clean(void);



// implementation


int main(int argc, char **argv)
{
  /* check command line arguments */
  if ( parse_cmdline(argc, argv) != 0 )
  {
    fprintf(stderr, "Unable to parse commandline.\n");
    exit(1);
  }

  fprintf(stdout, "Initialising...\n");

  conf.fsm_state = FSM_INIT;
  conf.train_id = 1;
  conf.train_packet_length = TRAIN_PACKET_LENGTH_MIN;
  conf.train_length = TRAIN_LENGTH_MIN;

  signal(SIGPIPE, signal_handler);
  signal(SIGHUP, signal_handler);
  signal(SIGALRM, signal_handler);

  //
  // TCP SOCKET INIT
  socklen_t len;
  int opt;

  if ( (conf.tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
  {
    fprintf(stderr, "Unable to open TCP socket.\n");
    exit(1);
  }

  opt = 1;
  if ( setsockopt(conf.tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 )
  {
    fprintf(stderr, "Unable set TCP socket options for address reuse.\n");
    exit(1);
  }

  bzero(&conf.tcp_addr, sizeof(conf.tcp_addr));
  conf.tcp_addr.sin_family = AF_INET;
  conf.tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  conf.tcp_addr.sin_port = htons(conf.tcp_port);

  if ( bind(conf.tcp_socket, (struct sockaddr *)&conf.tcp_addr, sizeof(conf.tcp_addr)) != 0 )
  {
    fprintf(stderr, "Unable to bind to TCP socket.\n");
    exit(1);
  }

  if ( listen(conf.tcp_socket, 1) < 0 )
  {
    fprintf(stderr, "Unable to listen on TCP socket.\n");
    exit(1);
  }
  // TCP SOCKET INIT - END
  //


  //
  // UDP SOCKET INIT
  if ( (conf.udp_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0 )
  {
    fprintf(stderr, "Unable to open UDP socket.\n");
    exit(1);
  }

  bzero(&conf.udp_addr, sizeof(conf.udp_addr));
  conf.udp_addr.sin_family = AF_INET;
  conf.udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  conf.udp_addr.sin_port = htons(conf.udp_port);

  if ( bind(conf.udp_socket,(struct sockaddr *)&conf.udp_addr, sizeof(conf.udp_addr)) != 0 )
  {
    fprintf(stderr, "Unable to bind to UDP socket.\n");
    exit(1);
  }

  // UDP SOCKET INIT - END
  //


//  init_system();

  fd_set read_fds;

  init_packet_train();

  uint32_t ctl_code, ctl_value;

  while ( conf.fsm_state != FSM_CLOSE )
  {
    
    conf.fsm_state = FSM_INIT;
    conf.failed_messages = 0;

    fprintf(stdout, "Listening ...\n");

    // wait for receiver to start a new measurement cycle
    len = sizeof(conf.tcp_cli_addr);
    if ( (conf.tcp_fd = accept(conf.tcp_socket, (struct sockaddr*)&conf.tcp_cli_addr, &len)) < 0 )
    {
      perror("OOPS! accept(conf.tcp_socket):");
      continue;
    }

    {
      // who has connected to us
      conf.receiver = gethostbyaddr((char*)&(conf.tcp_cli_addr.sin_addr), sizeof(conf.tcp_cli_addr.sin_addr), AF_INET);
      fprintf(stdout, "Session initiated by %s\n", (NULL == conf.receiver) ? "unknown" : conf.receiver->h_name);

      // we have the receiver's TCP address sorted out, let's get the UDP address
      bzero((char *)&conf.udp_cli_addr, sizeof(conf.udp_cli_addr));
      conf.udp_cli_addr.sin_family = AF_INET;
      conf.udp_cli_addr.sin_addr.s_addr = conf.tcp_cli_addr.sin_addr.s_addr;
      conf.udp_cli_addr.sin_port = htons(conf.udp_cli_port);

      FD_ZERO(&read_fds);

      // loop until session wants to end
      while ( conf.fsm_state != FSM_END && conf.fsm_state != FSM_CLOSE )
      {
        FD_SET(conf.tcp_fd, &read_fds);  

        if ( select(conf.tcp_fd + 1, &read_fds, NULL, NULL, NULL) == -1 )
        {
          perror("OOPS! select(): ");
          conf.fsm_state = FSM_END;            
        }

        if ( FD_ISSET(conf.tcp_fd, &read_fds) )
        {
          int ret;
          if ( (ret=receive_control_message(conf.tcp_fd, &ctl_code, &ctl_value)) == 0)
          {
            switch ( ctl_code )
            {
              case MSG_SESSION_INIT:
                ulog(LOG_INFO, "Intialising session.\n");     
                break;
              case MSG_SESSION_END:
                ulog(LOG_INFO, "Ending session.\n");
                conf.fsm_state = FSM_END;
                break;
              case MSG_SESSION_CLIENT_UDP_PORT_SET:
                conf.udp_cli_port = (short)ctl_value;
                conf.udp_cli_addr.sin_port = htons(conf.udp_cli_port);
                ulog(LOG_INFO, "Setting client UDP listen port to: %u\n", conf.udp_cli_port);
                break;
              case MSG_RTT_SYNC:
                ulog(LOG_DEBUG, "RTT Sync\n");
                send_control_message(conf.tcp_fd, MSG_RTT_SYNC, (0xffffff-ctl_value));
                break;
              case MSG_TRAIN_SPACING_SET:
                conf.train_spacing = (double)ctl_value;
                ulog(LOG_INFO, "Setting train spacing to: %.0fus\n", conf.train_spacing);
                break;
              case MSG_TRAIN_SPACING_MIN_SET:
                conf.train_spacing_min = (double)ctl_value;
                ulog(LOG_INFO, "Setting minimum train spacing to: %.0fus\n", conf.train_spacing_min);
                break;
              case MSG_TRAIN_SPACING_MAX_SET:
                conf.train_spacing_max = (double)ctl_value;
                ulog(LOG_INFO, "Setting maximum train spacing to: %.0fus\n", conf.train_spacing_max);
                break;
              case MSG_TRAIN_LENGTH_SET:
                conf.train_length = ctl_value;
                ulog(LOG_INFO, "Setting train length to: %u packets\n", conf.train_length);
                break;
              case MSG_TRAIN_LENGTH_MAX_SET:
                conf.train_length_max = ctl_value;
                ulog(LOG_INFO, "Setting maximum train length to: %u packets\n", conf.train_length_max);
                break;
              case MSG_TRAIN_PACKET_LENGTH_SET:
                conf.train_packet_length = ctl_value;
                ulog(LOG_INFO, "Setting train packet length to: %u packets\n", conf.train_packet_length);
                break;
              case MSG_TRAIN_PACKET_LENGTH_MIN_SET:
                conf.train_packet_length_min = ctl_value;
                ulog(LOG_INFO, "Setting minimum train packet length to: %u bytes\n", conf.train_packet_length_min);
                break;
              case MSG_TRAIN_PACKET_LENGTH_MAX_SET:
                conf.train_packet_length_max = ctl_value;
                ulog(LOG_INFO, "Setting maximum train packet length to: %u bytes\n", conf.train_packet_length_max);
                break;
              case MSG_TRAIN_ID_SET:
                conf.train_id = ctl_value;
                ulog(LOG_INFO, "Setting train ID to: %d\n", conf.train_id);     
                break;
              case MSG_TRAIN_SEND:
                send_train(conf.train_id, conf.train_length, conf.train_packet_length, &conf.udp_cli_addr);
                // set alarm for confirmation of acknowledgement
                alarm(10);
                break;
              case MSG_TRAIN_RECEIVE_ACK:
              case MSG_TRAIN_RECEIVE_FAIL:
                alarm(0);
                break;
              default:
                ulog(LOG_INFO, "Unknown code received: %d\n", ctl_value);
                break;
            }
          }
          else if ( ret == 2 )
          {
            ulog(LOG_INFO, "Terminating session due closed connection.\n");
            conf.fsm_state = FSM_END;            
          }
        }
      }

      alarm(0);
      close(conf.tcp_fd);
    }
  }

  exit( exit_clean() );
}

void signal_handler(int signal)
{
  // sigpipe is likely the client died so just abort the connection
  if ( signal == SIGPIPE )
    conf.fsm_state = FSM_END;
  else if ( signal == SIGALRM )
  {
    send_control_message(conf.tcp_fd, MSG_TRAIN_SENT, conf.train_id);
    alarm(10);
  }
  else
    conf.fsm_state = FSM_CLOSE;
}


int parse_cmdline(int argc, char **argv)
{
  // set sane defaults
  conf.tcp_port = DEFAULT_TCP_SERVER_PORT;
  conf.udp_port = DEFAULT_UDP_SERVER_PORT;

  conf.udp_cli_port = DEFAULT_UDP_CLIENT_PORT;

  int c;
  int long_option_index = 0;
  static struct option long_options[] = {
    {"help", 0, NULL, '?'},
    {"version", 0, NULL, 'V'},
    {"port", 1, NULL, 'f'},
    {0, 0, 0, 0}
  };

  while( (c=getopt_long(argc, argv, "?Vp:", long_options, &long_option_index)) != EOF )
  {
    switch (c)
    {
      case '?':
        usage(argv[0]);
        exit(0);
        break;
      case 'V':
        banner();
        exit(0);
        break;
      case 'p':
        conf.tcp_port = atoi(optarg);
        if ( conf.tcp_port == 0 )
        {
          fprintf(stderr, "FATAL: TCP listen port %d is not valid!\n", conf.tcp_port);
          exit(1);
        }
        break;
    }
  }

  return 0;
}

void banner()
{
  fprintf(stderr, ""
    "   .' ___\n"
    "  ][__]_[  Loco v%s.%s.%s %s\n"
    " (____|_|  (C) Copyright 2011 Ian Firns (firnsy@securixlive.com)    \n"
    " /oo-OOOO\n"
    "\n", VER_MAJOR, VER_MINOR, VER_REV,
#ifdef DEBUG
"DEBUG "
#else
""
#endif
  );
}

void usage(const char *program_name)
{
  fprintf(stdout, "\n");
  fprintf(stdout, "USAGE: %s [-options]\n", program_name);
  fprintf(stdout, "\n");
  fprintf(stdout, " General Options:\n");
  fprintf(stdout, "  -?        You're reading it.\n");
  fprintf(stdout, "  -V        Version and compiled in options.\n");
  fprintf(stdout, "  -p <port> Specify C&C listen port (TCP).\n");
  fprintf(stdout, "\n");
  fprintf(stdout, " Long Options:\n");
  fprintf(stdout, "  --help    Same as '?'\n");
  fprintf(stdout, "  --version Same as 'V'\n");
  fprintf(stdout, "\n");
}

int init_packet_train()
{
  int ret = 0;

  // generate a seed for randomizing
  gettimeofday(&conf.time_now, (struct timezone*)0);
  srandom(conf.time_now.tv_sec);

  if ( (conf.random_packet = malloc(TRAIN_PACKET_LENGTH_MAX)) != NULL )
  {
    int i;

    // create random payload to compensate for any payload compression
    for (i=0; i<TRAIN_PACKET_LENGTH_MAX-1; i++)
      conf.random_packet[i]=(char)(random() & 0xff);
  }
  else
    ret = 1;

  return ret;
}

char * create_packet_train(uint32_t train_id, uint32_t packet_id, unsigned int packet_length)
{
  char *packet_train = NULL;

  uint32_t train_id_n = htonl(train_id);
  uint32_t packet_id_n = htonl(packet_id);

  // ensure we meet the minimum/maximum packet length constraints
  packet_length = (packet_length < TRAIN_PACKET_LENGTH_MIN ) ? TRAIN_PACKET_LENGTH_MIN : packet_length;
  packet_length = (packet_length > TRAIN_PACKET_LENGTH_MAX ) ? TRAIN_PACKET_LENGTH_MAX : packet_length;

  if ( (packet_train = malloc(packet_length*sizeof(char))) != NULL )
  {
    // insert master random payload    
    memcpy(packet_train, conf.random_packet, packet_length*sizeof(char));
   
    // insert identifiers
    memcpy(packet_train, &train_id_n, sizeof(uint32_t));
    memcpy(packet_train + sizeof(uint32_t), &packet_id_n, sizeof(uint32_t));

  }

  return packet_train;
}

int send_train(uint32_t train_id, unsigned int length, unsigned int packet_length, const struct sockaddr_in *client_address)
{
  int i, n;
  char *packet_train[length];

  ulog(LOG_DEBUG, "Building train ...\n");

  // build the train
  for (i=0; i<length; i++)
    packet_train[i] = create_packet_train(train_id, (uint32_t)i, packet_length);

  ulog(LOG_DEBUG, "Sending train ...\n");

  // send the train
  for (i=0; i<length; i++)
  {
    if ( (n=sendto(conf.udp_socket, packet_train[i], packet_length, 0, (struct sockaddr *)client_address, sizeof(struct sockaddr_in))) < packet_length )
    {
      ulog(LOG_DEBUG, "Incomplete packet sent [%d, %d < %d] (%s)\n", i, n, packet_length, strerror(errno));
    }
  }

  send_control_message(conf.tcp_fd, MSG_TRAIN_SENT, train_id);

  ulog(LOG_DEBUG, "Dismantling train ...\n");

  // free the train
  for (i=0; i<length; i++)
    free(packet_train[i]);

  return 0;
}


int exit_clean()
{
  free(conf.random_packet);

  return 0;
}
