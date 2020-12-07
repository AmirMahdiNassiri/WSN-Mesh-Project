#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "distance_estimator.h"

// ======================================== CONST Configurations ======================================== //

const int ACCEPTABLE_THRESHOLD = 20;
const int CALIBRATION_START_MIN = 255 - ACCEPTABLE_THRESHOLD;
const int CALIBRATION_START_MAX = 255;
const int CALIBRATION_END_MIN = 20;
const int CALIBRATION_END_MAX = CALIBRATION_END_MIN + ACCEPTABLE_THRESHOLD;

const float MIN_PROXIMITY_DISTANCE = 0.05;
const float MAX_PROXIMITY_DISTANCE = 0.24;
const float PROXIMITY_TO_METER = MAX_PROXIMITY_DISTANCE / 235;

const int ENVIRONMENTAL_FACTOR = 2 * 10;

// ======================================== Global Variables ======================================== //

int self_proximity;
int self_temperature;
float average_node_temperature;

int current_nodes = 0;
struct node_data nodes_data[MAX_NODES];

// ======================================== Functions ======================================== //

void print_node_status(struct node_data n)
{
    printf("name: %s address: 0x%04x max_proximity: %d max_rssi: %d is_calibrated: %d rssi_distance_factor: %f last_rssi: %d estimated_distance: %f\n", 
        n.name, n.address, n.max_proximity, n.max_rssi,
        n.is_calibrated, n.rssi_distance_factor,
        n.last_rssi, n.estimated_distance);
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

int add_node_if_not_exists(uint16_t address, char *name)
{
    int found_index = find_node(address);

    if (found_index != -1)
        return found_index;

    nodes_data[current_nodes].address = address;
    strcpy(nodes_data[current_nodes].name, name);

    current_nodes++;

    return current_nodes - 1;
}

double calculate_measured_power(int rssi, float distance)
{
    if (distance == 0)
        distance = MIN_PROXIMITY_DISTANCE;

    double d = log10(distance);
    d *= ENVIRONMENTAL_FACTOR;
    double measured_power = d + rssi;

    return measured_power;
}

void update_average_temperature()
{
    float new_value = 0;

    // Add other nodes temperature
    for (int i = 0; i < current_nodes; i++)
    {
        new_value += nodes_data[i].temperature;
    }

    // Add self temperature
    new_value += self_temperature;

    // Get the average
    new_value /= current_nodes + 1;

    average_node_temperature = new_value;
}

void update_node_estimated_distance(struct node_data *n)
{
    if (n->is_calibrated == 0)
        return;

    n->estimated_distance = pow(10, (n->rssi_distance_factor - n->last_rssi)/ENVIRONMENTAL_FACTOR);
}

void check_node_calibration(struct node_data *n)
{
    if (is_valid_calibration(n->max_proximity))
    {
        // NOTE: Remember that proximity values are highest when close, lowest when furthest
        
        // NOTE: See the following page for the formula:
        // https://iotandelectronics.wordpress.com/2016/10/07/how-to-calculate-distance-from-the-rssi-value-of-the-ble-beacon/#:~:text=At%20maximum%20Broadcasting%20Power%20(%2B,Measured%20Power%20(see%20below).

        double measured_power = calculate_measured_power(n->max_rssi,
            (255 - n->max_proximity) * PROXIMITY_TO_METER);

        n->rssi_distance_factor = measured_power;

        n->is_calibrated = 1;
        update_node_estimated_distance(n);
    }
    else
    {
        n->is_calibrated = 0;
    }
}

void set_self_node_proximity(int proximity)
{
    self_proximity = proximity;
}

void set_self_node_temperature(int temperature)
{
    self_temperature = temperature;
}

int is_valid_calibration(int proximity)
{
    if (CALIBRATION_START_MIN <= proximity && proximity <= CALIBRATION_START_MAX)
        return 1;
    
    return 0;
}

int check_valid_calibration()
{
    return is_valid_calibration(self_proximity);
}

int calibrate_node(uint16_t address, char *name, int proximity, int rssi)
{
    set_self_node_proximity(proximity);

    int node_index = add_node_if_not_exists(address, name);

    if (nodes_data[node_index].max_proximity < proximity)
        nodes_data[node_index].max_proximity = proximity;

    if (nodes_data[node_index].max_rssi < rssi)
        nodes_data[node_index].max_rssi = rssi;

    if (check_valid_calibration())
    {
        int first_calibration = !nodes_data[node_index].is_calibrated;

        check_node_calibration(&nodes_data[node_index]);
        print_status_update();
        
        return first_calibration;
    }

    return -1;
}

void update_node_data(uint16_t address, int rssi, int temperature)
{
    int node_index = find_node(address);

    if (node_index != -1)
    {
        nodes_data[node_index].last_rssi = rssi;
        nodes_data[node_index].temperature = temperature;
        update_average_temperature();
        update_node_estimated_distance(&nodes_data[node_index]);
        print_status_update();
    }
}