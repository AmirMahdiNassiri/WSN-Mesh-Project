#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "mesh_app.h"
#include "mesh.h"

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

double average_node_temperature;

struct node_data self_node_data;

int current_nodes = 0;
struct node_data neighbor_nodes_data[MAX_NODES];

// ======================================== Functions ======================================== //

void print_node_status(struct node_data n)
{
    printf("name: %s address: 0x%04x calibration_step: %d is_calibrated: %d rssi_distance_factor: %.1f rssi: %d distance: %.1f temperature: %.1f humidity: %d neighbor_distances: '%s'\n", 
        n.name, n.address, n.calibration_step,
        n.is_calibrated, n.rssi_distance_factor,
        n.rssi, n.distance, 
        n.temperature, n.humidity,
        n.neighbor_distances);
}

void print_status_update()
{
    printk("==================== MESH APP STATUS UPDATE ====================\n");

    printk("current_nodes: %d\n", current_nodes);

    for (int i = 0; i < current_nodes; i++)
    {
        printk("%d) ", i);
        print_node_status(neighbor_nodes_data[i]);
    }
    
    printk("================================================================\n");
}

void initialize_app()
{
    for (int i = 0; i < MAX_NODES; i++)
    {
        struct node_data n;

        for (int z = 0; z < NAME_SIZE; z++)
        {
            n.name[z] = '\0';
        }

        n.address = 0;
        
        n.calibration_step = 0;
        n.is_calibrated = 0;

        for (int j = 0; j < CALIBRATION_STEPS; j++)
        {
            n.calibration_proximity_values[j] = 0;
            n.calibration_rssi_values[j] = 0;
        }

        for (int k = 0; k < NEIGHBOR_DISTANCES_LENGTH; k++)
        {
            n.neighbor_distances[k] = '\0';
        }
        
        neighbor_nodes_data[i] = n;
    }

    print_status_update();
}

int find_node(uint16_t address)
{
    for (int i = 0; i < current_nodes; i++)
    {
        if (neighbor_nodes_data[i].address == address)
            return i;
    }
    
    return -1;
}

int add_node_if_not_exists(uint16_t address, char *name)
{
    int found_index = find_node(address);

    if (found_index != -1)
        return found_index;

    neighbor_nodes_data[current_nodes].address = address;
    strcpy(neighbor_nodes_data[current_nodes].name, name);

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
    double new_value = 0;

    // Add other nodes temperature
    for (int i = 0; i < current_nodes; i++)
    {
        new_value += neighbor_nodes_data[i].temperature;
    }

    // Add self temperature
    new_value += self_node_data.temperature;

    // Get the average
    new_value /= current_nodes + 1;

    average_node_temperature = new_value;
}

void update_node_estimated_distance(struct node_data *n)
{
    if (n->is_calibrated == 0)
        return;

    n->distance = pow(10, (n->rssi_distance_factor - n->rssi)/ENVIRONMENTAL_FACTOR);
}

void check_node_calibration(struct node_data *n)
{
    if (CALIBRATION_STEPS <= n->calibration_step)
    {
        printf("Calibrate finalizing.\n");

        double measured_power_average = 0;

        for (int i = 0; i < n->calibration_step; i++)
        {
            // NOTE: Remember that proximity values are highest when close, lowest when furthest
                        
            // NOTE: See the following page for the formula:
            // https://iotandelectronics.wordpress.com/2016/10/07/

            double measured_power = calculate_measured_power(n->calibration_rssi_values[i],
                (255 - n->calibration_proximity_values[i]) * PROXIMITY_TO_METER);

            measured_power_average += measured_power / n->calibration_step;
        }

        printf("measured_power_average calculated: %f.\n", measured_power_average);

        n->rssi_distance_factor = measured_power_average;
        n->is_calibrated = 1;

        update_node_estimated_distance(n);
    }
    else
    {
        n->is_calibrated = 0;
    }
}

int is_valid_calibration(int proximity)
{
    if (CALIBRATION_START_MIN <= proximity && proximity <= CALIBRATION_START_MAX)
        return 1;
    
    return 0;
}

int calibrate_node(uint16_t address, char *name, int proximity, int rssi)
{
    int node_index = add_node_if_not_exists(address, name);

    node_data *node = &neighbor_nodes_data[node_index];

    if (is_valid_calibration(proximity) && node->calibration_step < CALIBRATION_STEPS)
    {
        node->calibration_proximity_values[node->calibration_step] = proximity;
        node->calibration_rssi_values[node->calibration_step] = rssi;
        node->calibration_step++;

        int first_calibration = !node->is_calibrated;
        check_node_calibration(node);

        print_status_update();
        
        return first_calibration;
    }

    return -1;
}

void get_self_node_message(char *buffer)
{
    if (strlen(self_node_data.name) == 0)
    {
        copy_bluetooth_name(self_node_data.name);
    }

    if (current_nodes > 0)
    {
        char neighbor_distances[10 * current_nodes];

        for (int i = 0; i < current_nodes; i++)
        {
            char node_distance[400];
            sprintf(node_distance, ",%04x:%.1f", 
                neighbor_nodes_data[i].address, neighbor_nodes_data[i].distance);

            strcpy(neighbor_distances, node_distance);
        }

        sprintf(buffer, "%s,%.1f,%d;%d%s", 
            self_node_data.name,
            self_node_data.temperature,
            self_node_data.humidity,
            current_nodes, neighbor_distances);
    }
    else
    {
        sprintf(buffer, "%s,%.1f,%d;%d", 
            self_node_data.name,
            self_node_data.temperature,
            self_node_data.humidity,
            current_nodes);
    }
}

void update_node_data(uint16_t address, int rssi, char* message_string)
{
    int node_index = find_node(address);

    if (node_index == -1)
        return;

    neighbor_nodes_data[node_index].rssi = rssi;

    char* parsing_copy = (char*)malloc(strlen(message_string) + 1);

    if (parsing_copy == NULL)
    {
        printf("Couldn't allocate memory for parsing_copy of the message.");
        return;
    }

    strcpy(parsing_copy, message_string);

    // Get the first segment containing node sensor data
    char* token = strtok(parsing_copy, ";");

    // Iterate through the node sensor values
    char* delimeter = ",";
    token = strtok(parsing_copy, delimeter);
    int token_index = 0;

    while (token != NULL) 
    {
        if (token_index == 1)
        {
            // First token indicates temperature
            neighbor_nodes_data[node_index].temperature = atof(token);
        }
        else if (token_index == 2)
        {
            // Second token indicates humidity
            neighbor_nodes_data[node_index].humidity = atoi(token);
        }
        else if (token_index > 2)
        {
            break;
        }
        
        token = strtok(NULL, delimeter);
        token_index++;
    }

    // Get the second segment containing node neighbor distances
    strcpy(parsing_copy, message_string);
    token = strtok(parsing_copy, ";");
    token = strtok(NULL, ";");

    strcpy(neighbor_nodes_data[node_index].neighbor_distances, token);

    update_average_temperature();
    update_node_estimated_distance(&neighbor_nodes_data[node_index]);
    print_status_update();
}