# Serial Communication Protocol in C

This project implements a low-level communication protocol for transferring files between two Linux-based computers connected via a serial cable. Using raw C and Linux system calls, the protocol is built from the ground up to handle the challenges of noisy, unreliable connections with robust error management and a sliding window frame-based approach.

## Overview

The goal of this project is simple: reliable file transfers over serial communication. By leveraging low-level C programming and Linux, the protocol offers direct hardware access and tight control over data transfer. With a frame-based design and a sliding window mechanism, it maximizes throughput while maintaining data integrity even in high-noise environments.

## Features

- **Low-Level Implementation:** Direct use of C and Linux system calls for precise control.
- **Serial Communication:** Designed for file transfer between two computers using a serial cable.
- **Frame-Based Protocol:** Data is structured into frames for organized, manageable transmission.
- **Sliding Window Mechanism:** Enhances efficiency by allowing multiple frames to be in transit concurrently.
- **Robust Error Handling:** Built-in fail safes to detect and recover from noise and transmission errors.

## Installation

### Prerequisites

- Linux operating system
- GCC or another compliant C compiler
- Make (for building the project)
