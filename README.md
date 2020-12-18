# Wireless Sensor Networks Mesh Project

This project contains source code for setting up a Bluetooth mesh network between [Reel Board] devices running [Zephyr RTOS].

The project was initiated as a course project for the COMP 7570 Wireless Sensor Networks instructed by [Dr. Rasit Eskicioglu] at the [University of Manitoba].

# Features
The [Reel Board]s connected via this project transmit sensor values such as:

  - Temperature
  - Humidity

Additionally, the distance between the nodes is estimated and reported in fractional metres.

![alt text][Pic 3]

# Zephyr Version

The project was built and tested using **Zephyr version 2.4**. Here is the information of the Zephyr version file:

> VERSION_MAJOR = 2

> VERSION_MINOR = 4

> PATCHLEVEL = 0

> VERSION_TWEAK = 0

> EXTRAVERSION = rc1

More specifically, the Zephyr folder was checked out from this [specific commit (3672feb)].

The only **required edition** to this version of Zephyr's source code is copying the following file:
```sh
EDITED-Kconfig
```
from the repository to the following Zephyr directory:

```sh
zephyr/subsys/bluetooth/mesh/
```

this 'EDITED-Kconfig' should **replace the Kconfig file already present** in the directory.

# Build Notes

Since the source project controls the nRF chip's **Bluetooth mesh configuration**, clearing this chip's ROM is **required once** to switch to the new mesh configuration. In order to do so, execute the following command:

```sh
pyocd erase -c chip -t nrf52
```

Also, during the first deployment, the boards need to be configured via [Nordic nRF Connect app]. For more information have a look at the [Mesh Badge sample] of Zephyr.

# Calibration
The boards require a **calibration step** before they can estimate their distance and generate the values. 

The calibration step involves putting two boards **facing each other in close vicinity** (around 1 to 2 centimetres) and pressing the button on one of them. The board sends calibration data to the other device, triggering calibration data to be sent by the other one as well. During the calibration, **5 Bluetooth messages** are transmitted from each of the boards, containing the proximity values. After the calibration, the boards are able to estimate their distance from each other with **high accuracy** (± 20 cm).

![alt text][Calibration]

The distance is estimated by the **RSSI** values and can have a higher error by the fluctuations in the signal strength.

# Getting Mesh Network Summary
The **mesh_app.c** exposes the following method:

```
void get_mesh_summary(char* buffer);
```

This method provides a comprehensive summary of **all the connected nodes' data** and puts it in the provided buffer array. A sample output of two connected boards looks like the following:

```
3d78,25.6,22;1,6afc:0.1
6afc,25.9,23;1,3d78:0.1
```

Each line contains the following node information:

```
<Address>,<Temperature>,<Humidity>;<NeighborCount>{,<NeighborAddress>,<NeighborDistance>}*NeighborCount
```

So in the aforementioned example, the first board (first line) has an address of '3d78', the temperature of '25.6' centigrade, the humidity of '22' percent, '1' connected neighbor with address '6afc' in '0.1' metres of its vicinity. While the other node (second line) has the address of '6afc' reporting the node with the address of '3d78' as its neighbor.

[//]: # (These are reference links used in the body of this note and get stripped out when the markdown processor does its job. There is no need to format nicely because it shouldn't be seen. Thanks SO - http://stackoverflow.com/questions/4823468/store-comments-in-markdown-syntax)


   [Reel Board]: <https://www.phytec.eu/product-eu/internet-of-things/reelboard/>
   [Zephyr RTOS]: <https://www.zephyrproject.org/>
   [University of Manitoba]: <https://sci.umanitoba.ca/cs/>
   [Dr. Rasit Eskicioglu]: <http://www.cs.umanitoba.ca/~rasit/>
   [specific commit (3672feb)]: <https://github.com/nrfconnect/sdk-zephyr/tree/3672feb3fa85b5f4e7207cc77af8d194798dd248>
   [Nordic nRF Connect app]: <https://www.nordicsemi.com/Software-and-tools/Development-Tools/nRF-Connect-for-mobile>
   [Mesh Badge sample]: <https://docs.zephyrproject.org/2.4.0/samples/boards/reel_board/mesh_badge/README.html>
   
   [Calibration]: https://github.com/AmirMahdiNassiri/WSN-Mesh-Project/blob/master/assets/Calibration.gif "Calibration"
   [Pic 3]: https://github.com/AmirMahdiNassiri/WSN-Mesh-Project/blob/master/assets/Pic%203.jpg "Connected nodes"
