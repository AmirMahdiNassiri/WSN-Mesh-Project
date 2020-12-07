#include <zephyr.h>

#define NAME_SIZE 8
#define MAX_NODES 10

extern const int CALIBRATION_START_MIN;
extern const int CALIBRATION_START_MAX;
extern const int CALIBRATION_END_MIN;
extern const int CALIBRATION_END_MAX;

struct node_data
{
    uint16_t address;

    char name[NAME_SIZE];

    int min_proximity;
    int min_rssi;

    int start_calibrated;

    int max_proximity;
    int max_rssi;

    int end_calibrated;

    int is_calibrated;
    float rssi_distance_factor;

    int last_rssi;
    float estimated_distance;
};

typedef struct node_data node_data;

extern int current_nodes;
extern struct node_data nodes_data[MAX_NODES];

void initialize_estimator(void);
int is_calibration_start(int);
int is_calibration_end(int);
int calibrate_node(uint16_t, char*, int, int);
void update_node_rssi(uint16_t, int);