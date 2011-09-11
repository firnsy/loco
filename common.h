#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <sys/time.h>

#define VER_MAJOR "0"
#define VER_MINOR "4"
#define VER_REV   "0"

#define BUFSIZE 1024

#define BW_COVAR_THRESHOLD .05

#define DEFAULT_UDP_CLIENT_PORT 32002

#define DEFAULT_TCP_SERVER_PORT 32001
#define DEFAULT_UDP_SERVER_PORT 0

#define RTT_COUNT_MAX 1024
#define RTT_VALID_COUNT 10

#define LATENCY_COUNT_MAX 1024
#define LATENCY_VALID_COUNT 400

#define PRELIM_COUNT_MAX 20
#define PRELIM_VALID_COUNT 10

#define TRAIN_LENGTH_MIN 2
#define TRAIN_LENGTH_MAX 32

#define TRAIN_PACKET_LENGTH_MIN 28
#define TRAIN_PACKET_LENGTH_MAX 1000
#define TRAIN_PACKET_LENGTH_SIZES 40

#define P1_TRAIN_DISCARD_COUNT_MAX 5

// ASSESSMENT TYPES
#define BW_ASSESS_UNKNOWN 0
#define BW_ASSESS_MODE    1
#define BW_ASSESS_NOMODE  2
#define BW_ASSESS_LBOUND  3
#define BW_ASSESS_QUICK   4


// OPERATING MODE
#define MODE_HELP       0x01
#define MODE_NET        0x02
#define MODE_NET_BIND   0x04
#define MODE_CSV        0x08
#define MODE_QUICK      0x10


// MODE CALCULATION

#define BIN_COUNT_TOLERANCE 0.015
#define BIN_COUNT_NOISE_THRESHOLD 15
  
#define ADR_THRESHOLD 0.9

// CONTROLL MESSAGES
#define MSG_SESSION_INIT                 1
#define MSG_SESSION_END                  2
#define MSG_RTT_SYNC                     3
#define MSG_SESSION_CLIENT_UDP_PORT_SET  5
#define MSG_TRAIN_ID_SET                 10
#define MSG_TRAIN_SPACING_SET            11
#define MSG_TRAIN_SPACING_MIN_SET        12
#define MSG_TRAIN_SPACING_MAX_SET        13
#define MSG_TRAIN_LENGTH_SET             14
#define MSG_TRAIN_LENGTH_MIN_SET         15
#define MSG_TRAIN_LENGTH_MAX_SET         16
#define MSG_TRAIN_PACKET_LENGTH_SET      17
#define MSG_TRAIN_PACKET_LENGTH_MIN_SET  18
#define MSG_TRAIN_PACKET_LENGTH_MAX_SET  19
#define MSG_TRAIN_SEND                   40
#define MSG_TRAIN_SENT                   41
#define MSG_TRAIN_RECEIVE_ACK            42
#define MSG_TRAIN_RECEIVE_FAIL           43

// FSM STATES
#define FSM_INIT      0
#define FSM_RTT_SYNC  1
#define FSM_PRELIM    5
#define FSM_P1        10
#define FSM_P1_CALC   40
#define FSM_P2        50
#define FSM_P2_CALC   70
#define FSM_CALC      80
#define FSM_CLOSE     90
#define FSM_END       99


// PUBLIC FUNCTIONS
int send_control_message(int fd, uint32_t code, uint32_t value);
int receive_control_message(int fd, uint32_t *code, uint32_t *value);

double time_delta_us(struct timeval t1, struct timeval t2);

void array_sort(double array[], double array_ordered[], unsigned int elements);
void array_print(double array[], unsigned int elements); 

double stat_array_interquartile_mean(double array[], unsigned int elements);
double stat_array_mean(double array[], unsigned int elements);
double stat_array_median(double array[], unsigned int elements);
double stat_array_std(double array[], unsigned int elements);
double stat_array_kurtosis(double array[], unsigned int elements);

int int_min(int a, int b);
int int_max(int a, int b);

#endif /* COMMON_H */
