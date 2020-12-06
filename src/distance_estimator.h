#include <zephyr.h>

extern const int CALIBRATION_START_MIN;
extern const int CALIBRATION_START_MAX;
extern const int CALIBRATION_END_MIN;
extern const int CALIBRATION_END_MAX;

void initialize_estimator(void);
int is_calibration_start(int);
int is_calibration_end(int);
int calibrate_node(uint16_t, int, int);
void update_node_rssi(uint16_t, int);