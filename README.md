# Network Protocol Implementation

## Overview
This project implements reliable data transfer protocols in C, specifically focusing on the Go-Back-N (GBN) and Selective Repeat (SR) protocols. These implementations demonstrate error detection, packet loss handling, and reliable data transfer over unreliable networks - core concepts in telecommunications and network gateway systems.

## Protocols Implemented
- **Go-Back-N (GBN)**: A sliding window protocol where the sender transmits multiple packets without waiting for acknowledgment but must retransmit all packets from the first unacknowledged packet when errors occur.
- **Selective Repeat (SR)**: An advanced sliding window protocol that allows the receiver to accept and buffer out-of-order packets and request retransmission of only specific lost packets.

## Key Features
- Error detection using checksums
- Packet acknowledgment and timeout mechanisms
- Buffer management for handling out-of-order packets
- Sequence number tracking and window management
- Performance metrics calculation (RTT, packet loss rate, etc.)

## Technical Implementation
- Written in C with socket programming libraries
- Circular buffer implementation for packet queuing
- Multithreaded approach for handling concurrent operations
- Network simulation with configurable packet loss and corruption rates

## Note on Repository Structure
This repository contains the server-side implementation of these protocols. While the complete project originally included client-side components, these files are not included in this repository. The server-side implementation demonstrates the core functionality of both protocols including the handling of acknowledgments, timeouts, and retransmissions.

## Usage
The implementation can be compiled and run with different parameters to simulate various network conditions:
- Packet loss probability
- Packet corruption probability
- Window size
- Timeout values

This allows for testing the protocols' resilience under different network scenarios that might be encountered in real-world telecommunications systems.
