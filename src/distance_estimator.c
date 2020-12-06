#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>

#include "distance_estimator.h"

// ======================================== CONST Configurations ======================================== //

const int ACCEPTABLE_THRESHOLD = 20;
const int CALIBRATION_START_MIN = 255 - ACCEPTABLE_THRESHOLD;
const int CALIBRATION_START_MAX = 255;
const int CALIBRATION_END_MIN = 20;
const int CALIBRATION_END_MAX = CALIBRATION_END_MIN + ACCEPTABLE_THRESHOLD;

const float PROXIMITY_TO_METER = 0.2 / 255;

#define MAX_NODES 10

// ======================================== Defined Data Types ======================================== //

struct node_data
{
    uint16_t address;

    int min_proximity;
    int min_rssi;

    int max_proximity;
    int max_rssi;

    int is_calibrated;
    float rssi_distance_factor;

    int last_rssi;
    float estimated_distance;
};

typedef struct node_data node_data;

// ======================================== Global Variables ======================================== //

static int current_proximity;
static struct node_data nodes_data[MAX_NODES];
static int current_nodes = 0;

// ======================================== Functions ======================================== //

void print_node_status(struct node_data n)
{
    char rssi_distance_factor_string[10];
    sprintf(rssi_distance_factor_string, "%f", n.rssi_distance_factor);

    char estimated_distance_string[10];
    sprintf(estimated_distance_string, "%f", n.estimated_distance);

    printk(" address: 0x%04x max_proximity: %d max_rssi: %d min_proximity: %d min_rssi: %d is_calibrated: %d rssi_distance_factor: %s last_rssi: %d estimated_distance: %s\n", 
        n.address, n.max_proximity, n.max_rssi,
        n.min_proximity, n.min_rssi, n.is_calibrated,
        rssi_distance_factor_string, n.last_rssi, estimated_distance_string);
}

void print_status_update()
{
    printk("=============== DISTANCE ESTIMATOR STATUS UPDATE ===============\n");

    printk("current_nodes: %d\n", current_nodes);

    for (int i = 0; i < current_nodes; i++)
    {
        printk("%d) ", i);
        print_node_status(nodes_data[i]);
    }
    
    printk("================================================================\n");
}

void initialize_estimator()
{
    for (int i = 0; i < MAX_NODES; i++)
    {
        struct node_data n;

        n.is_calibrated = 0;

        n.max_proximity = -1;
        n.max_rssi = -200;

        n.min_proximity = 300;
        n.min_rssi = 200;
        
        nodes_data[i] = n;
    }

    print_status_update();
}

int find_node(uint16_t address)
{
    for (int i = 0; i < current_nodes; i++)
    {
        if (nodes_data[i].address == address)
            return i;
    }
    
    return -1;
}

int add_node_if_not_exists(uint16_t address)
{
    int found_index = find_node(address);

    if (found_index != -1)
        return found_index;

    nodes_data[current_nodes++].address = address;

    return current_nodes - 1;
}

void update_node_estimated_distance(struct node_data *n)
{
    if (n->is_calibrated == 0)
        return;

    int rssi_diff = abs(n->max_rssi - n->last_rssi);
    n->estimated_distance = rssi_diff * n->rssi_distance_factor;
}

void check_node_calibration(struct node_data *n)
{
    if (is_calibration_start(n->max_proximity) && is_calibration_end(n->min_proximity))
    {
        int rssi_diff = abs(n->max_rssi - n->min_rssi);
        int proximity_diff = abs(n->max_proximity - n->min_proximity);

        n->rssi_distance_factor = (proximity_diff * PROXIMITY_TO_METER) / rssi_diff;
        n->is_calibrated = 1;
        update_node_estimated_distance(n);
    }
    else
    {
        n->is_calibrated = 0;
    }
}

void set_current_proximity(int proximity)
{
    current_proximity = proximity;
}

int is_calibration_start(int proximity)
{
    if (CALIBRATION_START_MIN <= proximity && proximity <= CALIBRATION_START_MAX)
        return 1;
    
    return 0;
}

int check_calibration_start()
{
    return is_calibration_start(current_proximity);
}

int is_calibration_end(int proximity)
{
    if (CALIBRATION_END_MIN <= proximity && proximity <= CALIBRATION_END_MAX)
        return 1;
    
    return 0;
}

int check_calibration_end()
{
    return is_calibration_end(current_proximity);
}

int calibrate_node(uint16_t address, int proximity, int rssi)
{
    set_current_proximity(proximity);

    int node_index = add_node_if_not_exists(address);

    if (nodes_data[node_index].max_proximity < proximity)
        nodes_data[node_index].max_proximity = proximity;

    if (nodes_data[node_index].max_rssi < rssi)
        nodes_data[node_index].max_rssi = rssi;

    if (proximity < nodes_data[node_index].min_proximity)
        nodes_data[node_index].min_proximity = proximity;

    if (rssi < nodes_data[node_index].min_rssi)
        nodes_data[node_index].min_rssi = rssi;

    if (check_calibration_start())
    {
        check_node_calibration(&nodes_data[node_index]);
        print_status_update();
        return 1;
    }
    
    if (check_calibration_end())
    {
        check_node_calibration(&nodes_data[node_index]);
        print_status_update();
        return 2;
    }

    return 0;
}

void update_node_rssi(uint16_t address, int rssi)
{
    int node_index = find_node(address);

    if (node_index != -1)
    {
        nodes_data[node_index].last_rssi = rssi;
        update_node_estimated_distance(&nodes_data[node_index]);
        print_status_update();
    }
}