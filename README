# Zephyr Project for demonstrating BLE and MQTT

This project uses ESP32 to test BLE and MQTT methods.

### Requirements
* Zephyr 3.7.99
* Zephyr SDK latest

### To note
* Follow the installation steps on the ZephyrRTOS homepage to install ZephyrRTOS and Zephyr SDK for the required toolchains.

* Use a virtual environment configured with the packages seen in <b>requirements.txt</b>. Maybe use virtualenvwrapper too.

* Download the blob for espressif hal using
```
$ west blobs fetch hal_espressif
```

### Run the project

```
$ git clone <this_project_url>
$ workon <zephyr_venv_name>
$ source <path_to>/zephyrproject/zephyr/zephyr-env.sh

$ cd <dir_to_project>
$ west build -p always -b <board_name> ./MQTT

$ west flash
```