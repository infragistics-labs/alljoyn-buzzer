# Alljoyn Raspberry Pi prototype

This is a sample prototype created to use the [AllJoyn](https://allseenalliance.org/) protocol on a [Raspberry Pi](https://www.raspberrypi.org/) to control and report the state of GPIO 4 & 17, and to enable audio broadcasting from a mic connected to the Pi.

## Description

There are three binaries:

1. [`buzzer_service`](buzzer_service.cc): Handles monitoring the bell ring (on input GPIO 17) and the unlocking of the electric lock (on output GPIO 4).
2. [`sink_client`](sink_client.cc): Discovers `sink_service`s on the AllJoyn network and subscribes to stream audio to them.
3. [`sink_service`](sink_service.cc): Publishes services to the AllJoyn network to receive audio streams and play them on the buzzer. This is exactly as the reference implementation of the AllJoyn Audio Service.

You can start the code as a service using the init scripts provided on `init`. 

## Building

To build the code, you need to have the AllJoyn source available and cross compiled for ARM. Configure the base path for your dependencies on the `Makefile` and then just run `make`.

For the alsa libs, you can cross compile them, compile the code on the Raspberry Pi itself, or just copy `/usr` and `/lib` from the Raspberry Pi (we used [raspbian](https://www.raspbian.org/)) to the `/dependencies_base_path/rpi-root` directory.

Binaries are created under the `bin/` directory.

## TODOs

1. Publish the AllJoyn source and libraries on a [PPA](https://launchpad.net/ubuntu/+ppas). At the moment, cross compiling is the hardest part, and would be greatly simplified by just being able to install AllJoyn deps from a repo.
2. Improve the audio services to connect to specific services. Now it streams to every `sink_service`, which means running a service _and_ a client on the same machine is not a good idea, since you'll loop audio forever.

The code was built only for testing purposes during the 2015 IG Hack Week. You won't find something really useful here, but you might look at the [`AlsaDataSource.cc`](AlsaDataSource.cc) code if you want to see how we handled the mic streaming from the Pi using AllJoyn.
