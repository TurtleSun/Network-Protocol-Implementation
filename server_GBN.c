#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>

/* ******************************************************************
   ARQ NETWORK EMULATOR: VERSION 1.1  J.F.Kurose
   MODIFIED by Chong Wang on Oct.21,2005 for csa2,csa3 environments

   This code should be used for PA2, unidirectional data transfer protocols 
   (from A to B)
   Network properties:
   - one way network delay averages five time units (longer if there
     are other messages in the channel for Pipelined ARQ), but can be larger
   - packets can be corrupted (either the header or the data portion)
     or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
     (although some can be lost).
**********************************************************************/

/* a "msg" is the data unit passed from layer 5 (teachers code) to layer  */
/* 4 (students' code).  It contains the data (characters) to be delivered */
/* to layer 5 via the students transport level protocol entities.         */
struct msg {
  char data[20];
  };

/* a packet is the data unit passed from layer 4 (students code) to layer */
/* 3 (teachers code).  Note the pre-defined packet structure, which all   */
/* students must follow. */
struct pkt {
   int seqnum;
   int acknum;
   int checksum;
   char payload[20];
   int sack[5];
};


/*- Your Definitions 
  ---------------------------------------------------------------------------*/
struct pkt lastPacketSent;
int currentSeqNum;
int expectedSeqNum;
int currAckNum;
int expectedAckNum;
int lastAckNum;
int lastAckNum_B;

int waiting_for_ack;      // Flag to track if we're waiting for an ACK
#define BUFFER_SIZE 50  // Size of the sender's buffer

struct buffer_item {
    struct pkt packet;
    int id; // for stats tracking
    int is_sent;        // Flag to track if packet has been sent
    int is_acked;       // Flag to track if packet has been acknowledged
    double send_time;   // Time when packet was sent
};

/* Add to your existing declarations */
struct buffer_item buffer[BUFFER_SIZE];  // Circular buffer for messages
int buffer_start;           // Start index of buffer
int buffer_end;            // End index of buffer
int buffer_count;          // Number of messages in buffer

/* Declarations for window */
int window_start; // Start index of window
int window_end;   // End index of window

struct pkt buffer_B[BUFFER_SIZE];  // Circular buffer for ack packets
int buffer_start_B = 0;           // Start index of buffer
int buffer_end_B = 0;            // End index of buffer
int buffer_count_B = 0;          // Number of messages in buffer

int sackArray[5] = {-1,-1,-1,-1,-1};
int toSend[12] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}; // meant to be WINDOW_SIZE

int window_start_B ; // Logical start of the window
int window_end_B ; // Logical end of the window
int expectedSeqNum ; // Next expected sequence number
int lastAckNum_B;            // Last acknowledged sequence number


/* Statsitic Variables */
int total_original_packets_sent;
int total_packets_retransmitted;
int total_packets_delivered_to_layer5;
int total_ack_packets_sent;
int total_timeouts;
int total_lost_packets;
double total_RTT;
int total_measured_RTTs;
int total_measured_CommTime;
double total_communication_time;

int packet_retransmitted[1000] = { 0 * 1000};
int idCounter = 0; // ID counter

int total_corrupted_packets = 0;
double timeStart;
double timeEnd;

/* Please use the following values in your program */

#define   A    0
#define   B    1
#define   FIRST_SEQNO   0

/*- Declarations ------------------------------------------------------------*/
void	restart_rxmt_timer(void);
void	tolayer3(int AorB, struct pkt packet);
void	tolayer5(char datasent[20]);

void	starttimer(int AorB, double increment);
void	stoptimer(int AorB);

void calculateRTT(struct buffer_item Item, double timeEnd);

void identify_packets_from_sack(int sack[]);
void send_packets_from_sack(int toSend[], int length);

/* WINDOW_SIZE, RXMT_TIMEOUT and TRACE are inputs to the program;
   Please set an appropriate value for LIMIT_SEQNO.
   You have to use these variables in your 
   routines --------------------------------------------------------------*/

extern int WINDOW_SIZE;      // size of the window
extern int LIMIT_SEQNO;      // when sequence number reaches this value,                                     // it wraps around
extern double RXMT_TIMEOUT;  // retransmission timeout
extern int TRACE;            // trace level, for your debug purpose
extern double time_now;      // simulation time, for your debug purpose

/********* YOU MAY ADD SOME ROUTINES HERE ********/
/* Helper function to calculate Checksum */
int calculateChecksum(struct pkt packet){
  int checksum = 0; 
  checksum += packet.seqnum;
  checksum += packet.acknum;
  for(int i = 0; i< 20; i++){
    checksum += packet.payload[i];
  }
  return checksum;
}

/* Helper function to calculate both Comm Time and RTT */
void calculateRTT(struct buffer_item Item, double timeEnd){
  timeStart = Item.send_time;
  if (packet_retransmitted[Item.id] == 1){
    // Dont add to RTT, only Comm
    total_communication_time = total_communication_time + (timeEnd - timeStart);
    total_measured_CommTime++;
  } else {
    total_communication_time = total_communication_time + (timeEnd - timeStart);
    total_RTT = total_RTT + (timeEnd - timeStart);
    total_measured_RTTs++;
    total_measured_CommTime++;
  }
}

/* Helper functions for buffer management */
int is_buffer_full(int AorB) {
  if (AorB){
    return buffer_count_B >= BUFFER_SIZE;
  } else {
    return buffer_count >= BUFFER_SIZE;
  }
}
int is_buffer_full_B() {
    return buffer_count_B >= BUFFER_SIZE;
}

int is_buffer_empty(int AorB) {
  if (AorB){
    return buffer_count_B == 0;
  } else {
    printf("buffer_count in is_buffer_empty = %d\n", buffer_count);
    return buffer_count == 0;
  }
}

struct pkt make_packet(struct msg message){
  struct pkt packet;
  packet.seqnum = currentSeqNum;
  packet.acknum = -1;
  memcpy(packet.payload, message.data, 20);
  packet.checksum = calculateChecksum(packet);
  //increment sequence number 
  currentSeqNum = (currentSeqNum + 1) % LIMIT_SEQNO;
  return packet;
}

/* Helper function to add packets into the buffer, includes a unique id for each packet and start_time */
void buffer_add(struct msg message) {
  buffer[buffer_end].packet = make_packet(message);
  buffer[buffer_end].is_sent = 0;
  buffer[buffer_end].is_acked = 0;
  buffer[buffer_end].id = idCounter;
  idCounter++; // increment for next ID
  buffer_end = (buffer_end + 1) % BUFFER_SIZE;
  buffer_count++;
  printf("A: Message buffered. Buffer count: %d\n", buffer_count);
}

/* Helper function to add packets into the Receiver buffer */
void buffer_add_B(struct pkt packet) {
    if (!is_buffer_full_B()) {
        buffer_B[buffer_end_B] = packet;
        buffer_end_B = (buffer_end_B + 1) % BUFFER_SIZE;
        buffer_count_B++;
        printf("B: Packet %d buffered. Buffer count: %d\n", packet.seqnum, buffer_count_B);
    }
}

/* Below two functions are used to remove items/packets from their respective buffers */
  struct buffer_item buffer_remove() {
    struct buffer_item item = buffer[buffer_start];
    buffer_start = (buffer_start + 1) % BUFFER_SIZE;
    buffer_count--;
    printf("A: Message dequeued. Buffer count: %d\n", buffer_count);
    return item;
}

struct pkt buffer_remove_B() {
    struct pkt packet = buffer_B[buffer_start_B];
    buffer_start_B = (buffer_start_B + 1) % BUFFER_SIZE;
    buffer_count_B--;
    printf("B: Packet %d removed from buffer. Buffer count: %d\n", packet.seqnum, buffer_count_B);
    return packet;
}

/* Behaviors within Buffer */

int isDuplicate_B(struct pkt packet) {
    int index = buffer_start_B;

    // Iterate through buffer B and check if packet.seqnum already exists in buffer
    while (index != buffer_end_B) {
        if (buffer_B[index].seqnum == packet.seqnum) {
            return 1; // Packet is a duplicate
        }
        index = (index + 1) % BUFFER_SIZE;  // Handle circular buffer indexing
    }

    return 0; // Packet is not a duplicate
}

int isDuplicate(struct pkt packet){
  int ackNum = packet.acknum;
  if (ackNum == lastAckNum) {
      return 1;
  }

  // If this packet is already marked as ACKed in our buffer, it's a duplicate
  for (int i = window_start; i != window_end; i = (i + 1) % BUFFER_SIZE) {
      if (buffer[i].packet.seqnum == ackNum && buffer[i].is_acked) {
          return 1;
      }
  }
  return 0;
}

void bubble_sort_buffer_B() {
    if (buffer_count_B <= 1) {
        printf("Buffer B is empty or has only one packet, no sorting needed.\n");
        return;
    }

    // Use bubble sort to sort buffer B based on seqnum
    int i, j;
    for (i = 0; i < buffer_count_B - 1; i++) {
        for (j = buffer_start_B; j != (buffer_start_B + buffer_count_B - 1) % BUFFER_SIZE; j = (j + 1) % BUFFER_SIZE) {
            int next_idx = (j + 1) % BUFFER_SIZE;
            
            // Compare seqnums of consecutive packets and swap if needed
            if (buffer_B[j].seqnum > buffer_B[next_idx].seqnum) {
                // Swap packets in buffer
                struct pkt temp = buffer_B[j];
                buffer_B[j] = buffer_B[next_idx];
                buffer_B[next_idx] = temp;
            }
        }
    }
    printf("Buffer B sorted based on seqnum.\n");
}


int is_within_window_B(int seqnum) {
    // Handles the wrap-around logic for the window
    if (window_end_B > window_start_B) {
        return (seqnum >= window_start_B && seqnum < window_end_B);
    } else {
        // Handles window wrap-around
        return (seqnum >= window_start_B || seqnum < window_end_B);
    }
}

/* Helper function to send packets in window */
void send_packets_in_window() {
    int count = 0; 
    int current = buffer_start;
    
    // Modified condition to use WINDOW_SIZE instead of window_end
    while (count < WINDOW_SIZE && current != buffer_end) {
      if (!buffer[current].is_sent) {
        printf("A: Sending packet %d\n", buffer[current].packet.seqnum);
        tolayer3(A, buffer[current].packet);
        buffer[current].is_sent = 1;
        buffer[current].send_time = time_now;
        total_original_packets_sent++;
        
        stoptimer(A);
        starttimer(A, RXMT_TIMEOUT);
      }
      current = (current + 1) % BUFFER_SIZE;
      count++;
    }
}

/* For SACK, A Side */
void identify_packets_from_sack(int sack[]) {

    int count = buffer_start;
    int toSendIndex = 0;

    // Initialize toSend to -1
    for (int i = 0; i < WINDOW_SIZE; i++) {
        toSend[i] = -1;
    }

    // populate toSend based on SACK and buffer_end
    for (int i = 0; i < WINDOW_SIZE; i++) {
        if (count == buffer_end) {
            break;  // Stop if we've reached buffer_end
        }

        int foundInSack = 0;

        // Check if the sequence number is present in the SACK
        for (int j = 0; j < 5; j++) {
            if (sack[j] == -1) {
                break;  // Stop searching if we reach an unused slot in SACK
            }
            if (sack[j] == buffer[count].packet.seqnum) {
                foundInSack = 1;  // Packet is acknowledged, skip it
                break;
            }
        }

        // Add to toSend if not found in SACK and already sent but unacknowledged
        if (!foundInSack && buffer[count].is_sent) {
            toSend[toSendIndex] = buffer[count].packet.seqnum;
            toSendIndex++;
        }

        // Move to the next packet in the buffer, wrapping around if necessary
        count = (count + 1) % BUFFER_SIZE;
    }

    send_packets_from_sack(toSend, toSendIndex);
}

/* Helper function called after indentifying packets from sack, Sending outstanding packets no in sack */
void send_packets_from_sack(int toSend[], int length){
  printf("A: Sending packets from SACK\n");

  for (int i = 0; i < length; i++) {
        int seqnumToSend = toSend[i];
        
        // Find the packet with seqnumToSend in the buffer
        for (int j = 0; j < WINDOW_SIZE; j++) {
            int bufferIndex = (buffer_start + j) % BUFFER_SIZE;  // Wrap-around for circular buffer

            // Check if the sequence number matches
            if (buffer[bufferIndex].packet.seqnum == seqnumToSend) {
                // Send the packet to layer3
                tolayer3(A, buffer[bufferIndex].packet);
                printf("A: SACK Retransmitting packet %d\n", buffer[bufferIndex].packet.seqnum);
                total_packets_retransmitted++;

                // Restart the timer for each packet being retransmitted
                stoptimer(A);
                starttimer(A, RXMT_TIMEOUT);
                break;
            }
        }
    }
}

//Helper function to shift packets in SACK Array over one
void addSackArray(int seqnum) {
  for (int i = 0; i < 5; i++) {
    if (sackArray[i] == -1) {
      sackArray[i] = seqnum;  // Insert in the first empty slot
      return;
    }
  }

  // If the array is full, shift elements to make space for the new sequence number
  for (int i = 1; i < 5; i++) {
    sackArray[i - 1] = sackArray[i];
  }
  sackArray[4] = seqnum;  // Place the new sequence number at the end
}

int inSackArray(int seqNum){
  for(int i = 0; i<5;i++){
    if(sackArray[i] != -1 && sackArray[i] == seqNum){
      return i;
    }
  }
  return -1;
}

void removeFromSackArray(int seqnum){
  int val = inSackArray(seqnum);
  if(val != -1){
    for(int i = val; i<4; i++){
      sackArray[i] = sackArray[i+1];
    }
    sackArray[4] = -1;
  }
}

/********* STUDENTS WRITE THE NEXT SEVEN ROUTINES *********/

/* called from layer 5, passed the data to be sent to other side */
void
A_output (message)
    struct msg message;
{
  // If buffer is full, drop the message
  if (is_buffer_full(A)) {

    return;
  }
  buffer_add(message);
  send_packets_in_window();
}

/* called from layer 3, when a packet arrives for layer 4 */
void
A_input(packet)
  struct pkt packet;
{	
  if (packet.checksum != calculateChecksum(packet)) {
    total_corrupted_packets++;
    return;
  }

  printf("Ack Packet: %d Sack:\n", packet.acknum);
  for(int i = 0; i < 5; i++) {
    printf("%d, ", packet.sack[i]);
    if(i == 4) {
      printf("\n");
    }
  }
  
  if (is_buffer_empty(A)){
    
    printf("A: Packet seqnum %d\n", packet.seqnum);
    return;
  }
  int ackNum = packet.acknum;
  if(isDuplicate(packet)){
    // Duplicate ACK - retransmit all unACKed packet not noted in sack
    identify_packets_from_sack(packet.sack);
    
  }else{
    // Expected ACK: move window
    printf("A: Recieved acked packet %d \n", packet.acknum);
    while(buffer[window_start].packet.seqnum != ackNum){
      struct buffer_item removedItem = buffer_remove();
      window_start = (window_start + 1) % BUFFER_SIZE;
      window_end = (window_end + 1) % BUFFER_SIZE;
    }
    struct buffer_item removedItem = buffer_remove();
    calculateRTT(removedItem, time_now);

    window_start = (window_start + 1) % BUFFER_SIZE;
    window_end = (window_end + 1) % BUFFER_SIZE;

    // no outstanding packets, since all packets have been ACKed
    if (window_start == buffer_end){ 
      stoptimer(A);
    } 

    // Update lastAckNum because the Acked packet was deleted
    lastAckNum = ackNum;
    printf("A: last acked packet %d \n", lastAckNum);
    
    // Send any new packets that might now fit in window
    send_packets_in_window();
  }
}

/* called when A's timer goes off */
void
A_timerinterrupt (void)
{
  struct pkt firstUnackedPacket = buffer[buffer_start].packet;
  printf("A: Retransmission timer fired, sent packet %d\n", firstUnackedPacket.seqnum);
  int retransmitted_id = buffer[buffer_start].id;
  packet_retransmitted[retransmitted_id] = 1;
  total_packets_retransmitted++;
  tolayer3(A,firstUnackedPacket);
  starttimer(A,RXMT_TIMEOUT);
} 

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void
A_init (void)
{
  buffer_start = 0;
  buffer_end = 0;
  buffer_count = 0;
  currentSeqNum = FIRST_SEQNO;
  lastAckNum = -1;
  
  // Initialize buffer
  for (int i = 0; i < BUFFER_SIZE; i++) {
      buffer[i].is_sent = 0;
      buffer[i].is_acked = 0;
      buffer[i].send_time = 0.0;
  }

  // Initialize window
  window_start = buffer_start;
  window_end = (window_start + WINDOW_SIZE);

  // Initialize statistics
  total_original_packets_sent = 0;
  total_packets_retransmitted = 0;
  total_RTT = 0.0;
  total_communication_time = 0.0;
  timeStart = 0.0;
  timeEnd = 0.0;
} 

/* called from layer 3, when a packet arrives for layer 4 at B*/
void
B_input (packet)
    struct pkt packet;
{
      struct pkt ackPacket;

    // Check the checksum first
    if (packet.checksum != calculateChecksum(packet)) {
        total_corrupted_packets++;
        return;  // If checksum is invalid, ignore the packet
    }

    printf("B: current expected sequence: %d\n", expectedSeqNum);
    // Assuming we are using the following constants:
// LIMIT_SEQNO: maximum sequence number (e.g., 255)
// WINDOW_SIZE: the size of the receiver's window

int window_end = (expectedSeqNum + WINDOW_SIZE - 1) % LIMIT_SEQNO;

// Check if packet is within the current window, accounting for circular wraparound
if (packet.seqnum == expectedSeqNum) {
    // In-sequence packet: deliver to layer 5
    tolayer5(packet.payload);
    total_packets_delivered_to_layer5++;
    printf("B: Received Packet %d, delivered to layer 5\n", packet.seqnum);

    // Move the expected sequence number forward
    expectedSeqNum = (expectedSeqNum + 1) % LIMIT_SEQNO;
    currAckNum = (currAckNum + 1) % LIMIT_SEQNO;

    // Check if we can deliver any buffered packets
    while (!is_buffer_empty(B) && buffer_B[buffer_start_B].seqnum == expectedSeqNum) {
        struct pkt nextPacket = buffer_remove_B();
        removeFromSackArray(expectedSeqNum);
        tolayer5(nextPacket.payload);
        total_packets_delivered_to_layer5++;
        printf("B: Buffered Packet %d delivered to layer 5\n", nextPacket.seqnum);
        expectedSeqNum = (expectedSeqNum + 1) % LIMIT_SEQNO;
        currAckNum = (currAckNum + 1) % LIMIT_SEQNO;
    }

    // Send cumulative ACK for the most recent packet
    for(int a = 0; a < 5; a++){
        int currValue = sackArray[a];
        ackPacket.sack[a] = currValue;
        printf("B1: Sack value: %d \n", ackPacket.sack[a]);
    }
    ackPacket.acknum = currAckNum;
    ackPacket.seqnum = -1;  // No sequence number for ACK
    ackPacket.checksum = calculateChecksum(ackPacket);
    tolayer3(B, ackPacket);
    total_ack_packets_sent++;
    printf("B: Sending ACK %d\n", ackPacket.acknum);

} else if (packet.seqnum != expectedSeqNum) {
    // Handle out-of-order packets, taking into account window wraparound

    // Case 1: Normal case (no wraparound)
    if (expectedSeqNum < window_end) {
        if (packet.seqnum > expectedSeqNum && packet.seqnum <= window_end) {
            // In-window packet: buffer it
            if (!isDuplicate_B(packet)) {
                buffer_add_B(packet);
                addSackArray(packet.seqnum);
                printf("B: Out-of-order packet %d, buffering it.\n", packet.seqnum);
                printf("B: Packet %d buffered. Buffer count: %d\n", packet.seqnum, buffer_count_B);
            } else {
                printf("B: Duplicate packet %d, discarding it.\n", packet.seqnum);
            }
        } else {
            // Out-of-window packet (beyond the window end, discard it)
            printf("B: Out-of-window packet %d, discarding it.\n", packet.seqnum);
        }
    }

    // Case 2: Wraparound case (window wraps past LIMIT_SEQNO)
    else {
        // The window wraps around, so we need to compare in a circular way
        if ((packet.seqnum >= expectedSeqNum && packet.seqnum < LIMIT_SEQNO) || (packet.seqnum < (window_end % LIMIT_SEQNO))) {
            // In-window packet: buffer it
            if (!isDuplicate_B(packet)) {
                buffer_add_B(packet);
                addSackArray(packet.seqnum);
                printf("B: Out-of-order packet %d, buffering it.\n", packet.seqnum);
                printf("B: Packet %d buffered. Buffer count: %d\n", packet.seqnum, buffer_count_B);
            } else {
                printf("B: Duplicate packet %d, discarding it.\n", packet.seqnum);
            }
        } else {
            // Out-of-window packet (discard it)
            printf("B: Out-of-window packet %d, discarding it.\n", packet.seqnum);
        }
    }

    // Send ACK for the last successfully received packet
    ackPacket.acknum = currAckNum;
    ackPacket.seqnum = -1;  // No sequence number for ACK
    for(int a = 0; a < 5; a++){
        int currValue = sackArray[a];
        ackPacket.sack[a] = currValue;
        printf("B2: Sack value: %d \n", ackPacket.sack[a]);
    }
    ackPacket.checksum = calculateChecksum(ackPacket);
    tolayer3(B, ackPacket);
    total_ack_packets_sent++;
    printf("B: Sending ACK %d\n", ackPacket.acknum);
} else {
    // If the sequence number is neither equal to the expected one nor within the window, discard the packet.
    printf("B: Out-of-window packet %d, discarding it.\n", packet.seqnum);
    ackPacket.acknum = currAckNum;
    ackPacket.seqnum = -1;  // No sequence number for ACK
    for(int a = 0; a < 5; a++){
        int currValue = sackArray[a];
        ackPacket.sack[a] = currValue;
        printf("B3: Sack value: %d \n", ackPacket.sack[a]);
    }
    ackPacket.checksum = calculateChecksum(ackPacket);
    tolayer3(B, ackPacket);
    total_ack_packets_sent++;
    printf("B: Sending ACK %d\n", ackPacket.acknum);
}

}

/* the following rouytine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void
B_init (void)
{
  expectedSeqNum = FIRST_SEQNO ;
  currAckNum = FIRST_SEQNO - 1;
  total_packets_delivered_to_layer5 = 0;
  total_ack_packets_sent = 0;
  lastAckNum_B = -1;
  window_start_B = FIRST_SEQNO; 
  window_end_B = (window_start_B + WINDOW_SIZE) % LIMIT_SEQNO; 
  expectedSeqNum = FIRST_SEQNO; 
  lastAckNum_B = -1;   
}

/* called at end of simulation to print final statistics */
void Simulation_done()
{
    /* TO PRINT THE STATISTICS, FILL IN THE DETAILS BY PUTTING VARIBALE NAMES. DO NOT CHANGE THE FORMAT OF PRINTED OUTPUT */
    printf("\n\n===============STATISTICS======================= \n\n");
    printf("Number of original packets transmitted by A: %d \n", total_original_packets_sent);
    printf("Number of retransmissions by A: %d \n", total_packets_retransmitted);
    printf("Number of data packets delivered to layer 5 at B: %d\n", total_packets_delivered_to_layer5);
    printf("Number of ACK packets sent by B: %d \n", total_ack_packets_sent);
    printf("Number of corrupted packets: %d \n", total_corrupted_packets);
    printf("Ratio of lost packets: %f \n", ((float)total_packets_retransmitted - total_corrupted_packets)/(((float)total_original_packets_sent + total_packets_retransmitted)+ total_ack_packets_sent));
    printf("Ratio of corrupted packets: %f \n", (total_corrupted_packets)/(((float)total_original_packets_sent + total_packets_retransmitted)+ total_ack_packets_sent - (total_packets_retransmitted - total_corrupted_packets)));
    printf("Average RTT: %f \n", total_RTT/total_measured_RTTs);
    printf("Average communication time: %f \n", total_communication_time/total_measured_CommTime);
    printf("==================================================");
    
    /* PRINT YOUR OWN STATISTIC HERE TO CHECK THE CORRECTNESS OF YOUR PROGRAM */
    printf("\nEXTRA: \n");
    printf("Packets in A buffer: %d \n", buffer_count);
    printf("Total measured RTTs: %f \n", total_RTT);
    printf("Total times measured RTT: %d \n", total_measured_RTTs);
    printf("Total measured communication time: %f \n", total_communication_time);
    printf("Total times measured communication time: %d \n", total_measured_CommTime);
    /* EXAMPLE GIVEN BELOW */
    /* printf("Example statistic you want to check e.g. number of ACK packets received by A : <YourVariableHere>"); */
}

/*****************************************************************
***************** NETWORK EMULATION CODE STARTS BELOW ***********
The code below emulates the layer 3 and below network environment:
  - emulates the tranmission and delivery (possibly with bit-level corruption
    and packet loss) of packets across the layer 3/4 interface
  - handles the starting/stopping of a timer, and generates timer
    interrupts (resulting in calling students timer handler).
  - generates message to be sent (passed from later 5 to 4)

THERE IS NOT REASON THAT ANY STUDENT SHOULD HAVE TO READ OR UNDERSTAND
THE CODE BELOW.  YOU SHOLD NOT TOUCH, OR REFERENCE (in your code) ANY
OF THE DATA STRUCTURES BELOW.  If you're interested in how I designed
the emulator, you're welcome to look at the code - but again, you should have
to, and you defeinitely should not have to modify
******************************************************************/


struct event {
   double evtime;           /* event time */
   int evtype;             /* event type code */
   int eventity;           /* entity where event occurs */
   struct pkt *pktptr;     /* ptr to packet (if any) assoc w/ this event */
   struct event *prev;
   struct event *next;
 };
struct event *evlist = NULL;   /* the event list */

/* Advance declarations. */
void init(void);
void generate_next_arrival(void);
void insertevent(struct event *p);
void printevlist(void);
void Simulation_done();


/* possible events: */
#define  TIMER_INTERRUPT 0
#define  FROM_LAYER5     1
#define  FROM_LAYER3     2

#define  OFF             0
#define  ON              1


int TRACE = 0;              /* for debugging purpose */
int fileoutput; 
double time_now = 0.000;
int WINDOW_SIZE;
int LIMIT_SEQNO;
double RXMT_TIMEOUT;
double lossprob;            /* probability that a packet is dropped  */
double corruptprob;         /* probability that one bit is packet is flipped */
double lambda;              /* arrival rate of messages from layer 5 */
int   ntolayer3;           /* number sent into layer 3 */
int   nlost;               /* number lost in media */
int ncorrupt;              /* number corrupted by media*/
int nsim = 0;
int nsimmax = 0;
unsigned int seed[5];         /* seed used in the pseudo-random generator */

int
main(int argc, char **argv)
{
   struct event *eventptr;
   struct msg  msg2give;
   struct pkt  pkt2give;

   int i,j;

   init();
   A_init();
   B_init();

   int loop = 0;
   while (1) {
    loop++;

        eventptr = evlist;            /* get next event to simulate */
        if (eventptr==NULL)
           goto terminate;
        evlist = evlist->next;        /* remove this event from event list */
        if (evlist!=NULL)
           evlist->prev=NULL;
        if (TRACE>=2) {
           printf("\nEVENT time: %f,",eventptr->evtime);
           printf("  type: %d",eventptr->evtype);
           if (eventptr->evtype==0)
               printf(", timerinterrupt  ");
             else if (eventptr->evtype==1)
               printf(", fromlayer5 ");
             else
             printf(", fromlayer3 ");
           printf(" entity: %d\n",eventptr->eventity);
           }
        time_now = eventptr->evtime;    /* update time to next event time */
        if (eventptr->evtype == FROM_LAYER5 ) {
            generate_next_arrival();   /* set up future arrival */
            /* fill in msg to give with string of same letter */
	    j = nsim % 26;
	    for (i=0;i<20;i++)
	      msg2give.data[i]=97+j;
	    msg2give.data[19]='\n';
	    nsim++;
	    if (nsim==nsimmax+1)
	      break;
	    A_output(msg2give);
	}
          else if (eventptr->evtype ==  FROM_LAYER3) {
            pkt2give.seqnum = eventptr->pktptr->seqnum;
            pkt2give.acknum = eventptr->pktptr->acknum;
            pkt2give.checksum = eventptr->pktptr->checksum;
            for(i = 0; i<5; i++)
              pkt2give.sack[i]=eventptr->pktptr->sack[i];
	    for (i=0;i<20;i++)
	      pkt2give.payload[i]=eventptr->pktptr->payload[i];
            if (eventptr->eventity ==A)      /* deliver packet by calling */
               A_input(pkt2give);            /* appropriate entity */
            else
               B_input(pkt2give);
            free(eventptr->pktptr);          /* free the memory for packet */
            }
          else if (eventptr->evtype ==  TIMER_INTERRUPT) {
	    A_timerinterrupt();
             }
          else  {
             printf("INTERNAL PANIC: unknown event type \n");
             }
        free(eventptr);
   }
terminate:
   Simulation_done(); /* allow students to output statistics */
   printf("Simulator terminated at time %.12f\n",time_now);
   return (0);
}


void
init(void)                         /* initialize the simulator */
{
  int i=0;
  printf("----- * Network Simulator Version 1.1 * ------ \n\n");
  printf("Enter number of messages to simulate: ");
  scanf("%d",&nsimmax);
  printf("Enter packet loss probability [enter 0.0 for no loss]:");
  scanf("%lf",&lossprob);
  printf("Enter packet corruption probability [0.0 for no corruption]:");
  scanf("%lf",&corruptprob);
  printf("Enter average time between messages from sender's layer5 [ > 0.0]:");
  scanf("%lf",&lambda);
  printf("Enter window size [>0]:");
  scanf("%d",&WINDOW_SIZE);
  LIMIT_SEQNO = WINDOW_SIZE*2; // set appropriately; here assumes SR
  printf("Enter retransmission timeout [> 0.0]:");
  scanf("%lf",&RXMT_TIMEOUT);
  printf("Enter trace level:");
  scanf("%d",&TRACE);
  printf("Enter random seed: [>0]:");
  scanf("%d",&seed[0]);
  for (i=1;i<5;i++)
    seed[i]=seed[0]+i;
  fileoutput = open("OutputFile", O_CREAT|O_WRONLY|O_TRUNC,0644);
  if (fileoutput<0) 
    exit(1);
  ntolayer3 = 0;
  nlost = 0;
  ncorrupt = 0;
  time_now=0.0;                /* initialize time to 0.0 */
  generate_next_arrival();     /* initialize event list */
}

/****************************************************************************/
/* mrand(): return a double in range [0,1].  The routine below is used to */
/* isolate all random number generation in one location.  We assume that the*/
/* system-supplied rand() function return an int in therange [0,mmm]        */
/*     modified by Chong Wang on Oct.21,2005                                */
/****************************************************************************/
int nextrand(int i)
{
  seed[i] = seed[i]*1103515245+12345;
  return (unsigned int)(seed[i]/65536)%32768;
}

double mrand(int i)
{
  double mmm = 32767;   /* largest int  - MACHINE DEPENDENT!!!!!!!!   */
  double x;                   /* individual students may need to change mmm */
  x = nextrand(i)/mmm;            /* x should be uniform in [0,1] */
  if (TRACE==0)
    printf("%.16f\n",x);
  return(x);
}


/********************* EVENT HANDLINE ROUTINES *******/
/*  The next set of routines handle the event list   */
/*****************************************************/
void
generate_next_arrival(void)
{
   double x,log(),ceil();
   struct event *evptr;
   //   char *malloc(); commented out by matta 10/17/2013

   if (TRACE>2)
       printf("          GENERATE NEXT ARRIVAL: creating new arrival\n");

   x = lambda*mrand(0)*2;  /* x is uniform on [0,2*lambda] */
                             /* having mean of lambda        */
   evptr = (struct event *)malloc(sizeof(struct event));
   evptr->evtime =  time_now + x;
   evptr->evtype =  FROM_LAYER5;
   evptr->eventity = A;
   insertevent(evptr);
}

void
insertevent(p)
   struct event *p;
{
   struct event *q,*qold;

   if (TRACE>2) {
      printf("            INSERTEVENT: time is %f\n",time_now);
      printf("            INSERTEVENT: future time will be %f\n",p->evtime);
      }
   q = evlist;     /* q points to header of list in which p struct inserted */
   if (q==NULL) {   /* list is empty */
        evlist=p;
        p->next=NULL;
        p->prev=NULL;
        }
     else {
        for (qold = q; q !=NULL && p->evtime > q->evtime; q=q->next)
              qold=q;
        if (q==NULL) {   /* end of list */
             qold->next = p;
             p->prev = qold;
             p->next = NULL;
             }
           else if (q==evlist) { /* front of list */
             p->next=evlist;
             p->prev=NULL;
             p->next->prev=p;
             evlist = p;
             }
           else {     /* middle of list */
             p->next=q;
             p->prev=q->prev;
             q->prev->next=p;
             q->prev=p;
             }
         }
}

void
printevlist(void)
{
  struct event *q;
  printf("--------------\nEvent List Follows:\n");
  for(q = evlist; q!=NULL; q=q->next) {
    printf("Event time: %f, type: %d entity: %d\n",q->evtime,q->evtype,q->eventity);
    }
  printf("--------------\n");
}



/********************** Student-callable ROUTINES ***********************/

/* called by students routine to cancel a previously-started timer */
void
stoptimer(AorB)
int AorB;  /* A or B is trying to stop timer */
{
 struct event *q /* ,*qold */;
 if (TRACE>2)
    printf("          STOP TIMER: stopping timer at %f\n",time_now);
/* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
 for (q=evlist; q!=NULL ; q = q->next)
    if ( (q->evtype==TIMER_INTERRUPT  && q->eventity==AorB) ) {
       /* remove this event */
       if (q->next==NULL && q->prev==NULL)
             evlist=NULL;         /* remove first and only event on list */
          else if (q->next==NULL) /* end of list - there is one in front */
             q->prev->next = NULL;
          else if (q==evlist) { /* front of list - there must be event after */
             q->next->prev=NULL;
             evlist = q->next;
             }
           else {     /* middle of list */
             q->next->prev = q->prev;
             q->prev->next =  q->next;
             }
       free(q);
       return;
     }
  printf("Warning: unable to cancel your timer. It wasn't running.\n");
}


void
starttimer(AorB,increment)
int AorB;  /* A or B is trying to stop timer */
double increment;
{

 struct event *q;
 struct event *evptr;
 // char *malloc(); commented out by matta 10/17/2013

 if (TRACE>2)
    printf("          START TIMER: starting timer at %f\n",time_now);
 /* be nice: check to see if timer is already started, if so, then  warn */
/* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next)  */
   for (q=evlist; q!=NULL ; q = q->next)
    if ( (q->evtype==TIMER_INTERRUPT  && q->eventity==AorB) ) {
      printf("Warning: attempt to start a timer that is already started\n");
      return;
      }

/* create future event for when timer goes off */
   evptr = (struct event *)malloc(sizeof(struct event));
   evptr->evtime =  time_now + increment;
   evptr->evtype =  TIMER_INTERRUPT;
   evptr->eventity = AorB;
   insertevent(evptr);
}


/************************** TOLAYER3 ***************/
void
tolayer3(AorB,packet)
int AorB;  /* A or B is trying to stop timer */
struct pkt packet;
{
 struct pkt *mypktptr;
 struct event *evptr,*q;
 // char *malloc(); commented out by matta 10/17/2013
 double lastime, x;
 int i;


 ntolayer3++;

 /* simulate losses: */
 if (mrand(1) < lossprob)  {
      nlost++;
      if (TRACE>0)
        printf("          TOLAYER3: packet being lost\n");
      return;
    }

/* make a copy of the packet student just gave me since he/she may decide */
/* to do something with the packet after we return back to him/her */
 mypktptr = (struct pkt *)malloc(sizeof(struct pkt));
 mypktptr->seqnum = packet.seqnum;
 mypktptr->acknum = packet.acknum;
 mypktptr->checksum = packet.checksum;
 for(int a = 0; a<5; a++){
  mypktptr->sack[a]=packet.sack[a];
 }
 for (i=0;i<20;i++)
   mypktptr->payload[i]=packet.payload[i];
 if (TRACE>2)  {
   printf("          TOLAYER3: seq: %d, ack %d, check: %d ", mypktptr->seqnum,
          mypktptr->acknum,  mypktptr->checksum);
   }

/* create future event for arrival of packet at the other side */
  evptr = (struct event *)malloc(sizeof(struct event));
  evptr->evtype =  FROM_LAYER3;   /* packet will pop out from layer3 */
  evptr->eventity = (AorB+1) % 2; /* event occurs at other entity */
  evptr->pktptr = mypktptr;       /* save ptr to my copy of packet */
/* finally, compute the arrival time of packet at the other end.
   medium can not reorder, so make sure packet arrives between 1 and 10
   time units after the latest arrival time of packets
   currently in the medium on their way to the destination */
 lastime = time_now;
/* for (q=evlist; q!=NULL && q->next!=NULL; q = q->next) */
 for (q=evlist; q!=NULL ; q = q->next)
    if ( (q->evtype==FROM_LAYER3  && q->eventity==evptr->eventity) )
      lastime = q->evtime;
 evptr->evtime =  lastime + 1 + 9*mrand(2);



 /* simulate corruption: */
 /* modified by Chong Wang on Oct.21, 2005  */
 if (mrand(3) < corruptprob)  {
    ncorrupt++;
    if ( (x = mrand(4)) < 0.75)
       mypktptr->payload[0]='?';   /* corrupt payload */
      else if (x < 0.875)
       mypktptr->seqnum = 999999;
      else
       mypktptr->acknum = 999999;
    if (TRACE>0)
        printf("          TOLAYER3: packet being corrupted\n");
    }

  if (TRACE>2)
     printf("          TOLAYER3: scheduling arrival on other side\n");
  insertevent(evptr);
}

void
tolayer5(datasent)
  char datasent[20];
{
  write(fileoutput,datasent,20);
}