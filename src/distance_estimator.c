#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h> /* strtoimax */

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

int str_to_uint16(const char *str, uint16_t *result)
{
    char *end;
    errno = 0;
    intmax_t val = strtoimax(str, &end, 10);

    if (errno == ERANGE || val < 0 || val > UINT16_MAX || end == str || *end != '\0')
        return false;

    *result = (uint16_t) val;


	// char* delimeter = ";";
	// char string[100] = "Hello!;We;are;103.456;-20;7bx9;learning;about;strtok";
    // // Extract the first token
    // char * token = strtok(string, delimeter);
    // // loop through the string to extract all other tokens
    // while( token != NULL ) {
    //     printf( " %s\n", token ); //printing each token
    //     token = strtok(NULL, delimeter);
    // }


// 	char *str = "12345";

//     int  y = atoi(str); // Using atoi()
//    printf("\nThe value of y : %d", y);

// 	char *test = "12.11";
// 	double temp = strtod(test, NULL);
// 	float ftemp = atof(test);
// 	printf("\nprice: %f, %f",temp,ftemp);

// 	uint16_t addr;

// 	str_to_uint16("31642", &addr);

// 	printf("CHAGHAL: 0x%04x", addr);

    return true;
}

void print_node_status(struct node_data n)
{
    printf("name: %s address: 0x%04x calibration_step: %d is_calibrated: %d rssi_distance_factor: %f last_rssi: %d estimated_distance: %f\n", 
        n.name, n.address, n.calibration_step,
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
        
        n.calibration_step = 0;
        n.is_calibrated = 0;
        
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
    if (CALIBRATION_STEPS <= n->calibration_step)
    {
        printf("Calibrate finalizing.\n");

        double measured_power_average = 0;

        for (int i = 0; i < n->calibration_step; i++)
        {
            // NOTE: Remember that proximity values are highest when close, lowest when furthest
                        
            // NOTE: See the following page for the formula:
            // https://iotandelectronics.wordpress.com/2016/10/07/

            double measured_power = calculate_measured_power(n->rssi_values[i],
                (255 - n->proximity_values[i]) * PROXIMITY_TO_METER);

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

void start_calibrating_node(uint16_t address, char *name, int proximity, int rssi)
{
    int node_index = add_node_if_not_exists(address, name);

    node_data *node = &nodes_data[node_index];

    node->is_calibrated = 0;
    node->calibration_step = 0;

    calibrate_node(address, proximity, rssi);
}

int calibrate_node(uint16_t address, int proximity, int rssi)
{
    int node_index = find_node(address);

    if (node_index == -1)
    {
        printf("Couldn't find node with address: 0x%04x Please start calibration again.", address);
        return -1;
    }

    node_data *node = &nodes_data[node_index];

    if (!is_valid_calibration(proximity))
        return -1;

    if (node->calibration_step < CALIBRATION_STEPS)
    {
        node->proximity_values[node->calibration_step] = proximity;
        node->rssi_values[node->calibration_step] = rssi;
        node->calibration_step++;

        check_node_calibration(node);
    }

    print_status_update();

    return node->is_calibrated;
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