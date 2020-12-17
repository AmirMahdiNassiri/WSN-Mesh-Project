#include <zephyr.h>

#define NAME_SIZE 8
#define MAX_NODES 10
#define CALIBRATION_STEPS 5
#define MAX_MESSAGE_SIZE 512

extern const int CALIBRATION_START_MIN;
extern const int CALIBRATION_START_MAX;
extern const int CALIBRATION_END_MIN;
extern const int CALIBRATION_END_MAX;

struct node_data
{
    char name[NAME_SIZE];

    uint16_t address;

    int calibration_step;
    int calibration_proximity_values[CALIBRATION_STEPS];
    int calibration_rssi_values[CALIBRATION_STEPS];
    int is_calibrated;
    double rssi_distance_factor;

    int rssi;
    double distance;

    double temperature;
    int humidity;

    int proximity;
    int light;
};

typedef struct node_data node_data;

extern struct node_data self_node_data;

extern int current_nodes;
extern struct node_data neighbor_nodes_data[MAX_NODES];
extern double average_node_temperature;

void initialize_estimator(void);
int is_valid_calibration(int);
int calibrate_node(uint16_t, char*, int, int);
void get_self_node_message(char*);
void update_node_data(uint16_t, int, char*);
void update_average_temperature(void);