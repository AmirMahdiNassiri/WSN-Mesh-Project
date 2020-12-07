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

    int max_proximity;
    int max_rssi;

    int is_calibrated;
    double rssi_distance_factor;

    int last_rssi;
    double estimated_distance;
};

typedef struct node_data node_data;

extern int current_nodes;
extern struct node_data nodes_data[MAX_NODES];

void initialize_estimator(void);
int is_valid_calibration(int);
int calibrate_node(uint16_t, char*, int, int);
void update_node_rssi(uint16_t, int);