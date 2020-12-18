# Wireless Sensor Networks Mesh Project

This project contains source code for setting up a Bluetooth mesh network between [Reel Board] devices running [Zephyr RTOS].

The project was initiated as a course project for the COMP 7570 Wireless Sensor Networks instructed by [Dr. Rasit Eskicioglu] at the [University of Manitoba].

# Features
The [Reel Board]s connected via this project transmit sensor values such as:

  - Temperature
  - Humidity

Additionally, the distance between the nodes is estimated and reported in fractional metres.

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

The calibration step involves putting two boards **facing each other in close vicinity** (around 1 to 2 centimetres) and pressing the button on one of them. The board sends calibration data to the other device, triggering calibration data to be sent by the other one as well. During the calibration, **5 Bluetooth messages** are transmitted from each of the boards, containing the proximity values. After the calibration, the boards are able to estimate their distance from each other with **high accuracy** (± 20 cm). The distance is estimated by the **RSSI** values and can have a higher error by the fluctuations in the signal strength.

[//]: # (These are reference links used in the body of this note and get stripped out when the markdown processor does its job. There is no need to format nicely because it shouldn't be seen. Thanks SO - http://stackoverflow.com/questions/4823468/store-comments-in-markdown-syntax)


   [Reel Board]: <https://www.phytec.eu/product-eu/internet-of-things/reelboard/>
   [Zephyr RTOS]: <https://www.zephyrproject.org/>
   [University of Manitoba]: <https://sci.umanitoba.ca/cs/>
   [Dr. Rasit Eskicioglu]: <http://www.cs.umanitoba.ca/~rasit/>
   [specific commit (3672feb)]: <https://github.com/nrfconnect/sdk-zephyr/tree/3672feb3fa85b5f4e7207cc77af8d194798dd248>
   [Nordic nRF Connect app]: <https://www.nordicsemi.com/Software-and-tools/Development-Tools/nRF-Connect-for-mobile>
   [Mesh Badge sample]: <https://docs.zephyrproject.org/2.4.0/samples/boards/reel_board/mesh_badge/README.html>
