#include <zephyr.h>

#define NAME_SIZE 8
#define MAX_NODES 10
#define CALIBRATION_STEPS 5

// neighbor_distances string contains the following information:
// "NeighborCount,<ID,Distance>*NeighborCount"
// NeighborCount -> 2 characters
// , -> 1 character
// ID -> 4 characters
// , -> 1 character
// distance (%.1f) -> 5 characters
#define NEIGHBOR_DISTANCES_LENGTH (2 + 1 + (4 + 1 + 5) * MAX_NODES)

// Message contains self node data + neighbor distances
#define MAX_MESSAGE_SIZE (100 + NEIGHBOR_DISTANCES_LENGTH)

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

    // See the comment for NEIGHBOR_DISTANCES_LENGTH
    char neighbor_distances[NEIGHBOR_DISTANCES_LENGTH];

    int proximity;
    int light;
};

typedef struct node_data node_data;

extern struct node_data self_node_data;

extern int current_nodes;
extern struct node_data neighbor_nodes_data[MAX_NODES];
extern double average_node_temperature;

void initialize_app(void);
int is_valid_calibration(int);
int calibrate_node(uint16_t, char*, int, int);
void get_self_node_message(char*);
void update_node_data(uint16_t, int, char*);
void update_average_temperature(void);