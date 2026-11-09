#include "traceroute.h"

// ****************************************************************************
// * Compute the Internet Checksum over an arbitrary buffer.
// * (written with the help of ChatGPT 3.5)
// ****************************************************************************
uint16_t checksum(unsigned short *buffer, int size) {
    unsigned long sum = 0;
    while (size > 1) {
        sum += *buffer++;
        size -= 2;
    }
    if (size == 1) {
        sum += *(unsigned char *) buffer;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (unsigned short) (~sum);
}


int main (int argc, char *argv[]) {
  std::string destIP;

  // ********************************************************************
  // * Process the command line arguments
  // ********************************************************************
  int opt = 0;
  while ((opt = getopt(argc,argv,"t:d:")) != -1) {

    switch (opt) {
    case 't':
      destIP = optarg;
      break;
    case 'd':
      LOG_LEVEL = atoi(optarg);
      break;
    case ':':
    case '?':
    default:
      std::cout << "useage: " << argv[0] << " -t [target ip] -d [Debug Level]" << std::endl;
      exit(-1);
    }
  }

  // ********************************************************************
  // * Allocate two 64 byte buffers. One for sending and one for receiving.
  // ********************************************************************
  uint8_t send_array[64] = {};
  uint8_t receive_array[64] = {};

  // ********************************************************************
  // * Fill in the whole buffer with a pattern of characters
  // ********************************************************************
  for (int i = 28; i < 64; i++){
    send_array[i] = i;
    receive_array[i] = i;
  }

  // ********************************************************************
  // * Fill in the fields of the IP header (No IP or checksum)
  // ********************************************************************
  
  struct iphdr *ip = (struct iphdr *)send_array; //Create an IP header struct which shares the same pointer as send_array
  ip->version = 4;
  ip->ihl = 5; //Header length divided by 4 for whatever reason
  ip->ttl = 0; //Time to live (Will change)
  ip->tos = 0; //Type of service
  ip->tot_len = htons(64); //Total packet length (Host to network short)
  ip->frag_off = htons(0); //No fragments, offset of 0
  ip->protocol = IPPROTO_ICMP; //Use ICMP
  int pton_result = inet_pton(AF_INET, destIP.c_str(), &ip->daddr);

  if (pton_result != 1){
      DEBUG << "Invalid Destination IP address" << ENDL;
      return -1;
  }

  ip->saddr = 0; //Set these to 0 and let operating system handle it.
  ip->check = 0;

  // ********************************************************************
  // * Fill in the fields of ICMP Header
  // ********************************************************************

  struct icmphdr *icmp = (struct icmphdr *)(send_array + 20); //Create a pointer to the icmp section of the send array

  icmp->type = ICMP_ECHO; //Type of ICMP
  icmp->code = 0; //Echo Requests use a 0 code
  icmp->checksum = 0; //Calculate 
  icmp->un.echo.id = htons(1234); //Id for ICMP echo request
  icmp->un.echo.sequence = htons(1); //Sequence Number for request

  // ********************************************************************
  // * Create the send and recieve sockets
  // ********************************************************************
  int sockFd;
  int recvFd;
  if ((sockFd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) == -1) {
    perror("socket");
    return -1;
  }

  if ((recvFd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) == -1) {
    perror("Socket");
    return -1;
  }

  // ********************************************************************
  // * Create While loop to send 31 packets.
  // ********************************************************************
  int current_ttl = 1;
  bool reply_not_received = true;

  //Create pointers to the ICMP and IP headers of the receive buffer
  struct iphdr* recv_ip_header = (struct iphdr*) receive_array;
  struct icmphdr* recv_icmp_header;

  while (current_ttl <= 31 && reply_not_received){
    ip->ttl = current_ttl; //Set the TTL in the IP header to current_ttl
    icmp->checksum = checksum((unsigned short *)icmp, 44); //Set the checksum in the ICMP header

    // ********************************************************************
    // * Use sendto to send the send_array object
    // ********************************************************************
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(destIP.c_str());

    //Send the object
    if (sendto(sockFd, send_array, sizeof(send_array), 0, (struct sockaddr *) &dest_addr, sizeof(dest_addr)) == -1) //Send the buffer using sendfrom
    {
    perror("sendto");
    return -1;
    }

    //Enter while loop to wait for response
    bool not_done_reading = true;
    int selectReturned = 0; //Stores the return values of the select function call
    int five_second_counter = 0;
    fd_set readFdSet; //Set of file descriptors we're listening for (i.e sockets)
    struct timeval timeout; //Struct that will handle the timeout

    while(not_done_reading && five_second_counter < 3) {
      //Set the timeout to 5 seconds
      timeout.tv_sec = 5;
      timeout.tv_usec = 0;

      //Set the bit in the set corresponding to file descriptor recvFd
      FD_ZERO(&readFdSet); //Empty readFdSet
      FD_SET(recvFd, &readFdSet); //Add the receive file descriptor to the set readFdSet

      //Call select()
      DEBUG << "Waiting up to 5 seconds to see if data becomes avalable" << ENDL;
      if ((selectReturned = select(recvFd + 1, &readFdSet, NULL, NULL, &timeout)) < 0){
        perror("select");
        return -1;
      } else if (selectReturned == 0) //Timeout occured
      {
         DEBUG << "Timeout Occured" << ENDL;
         five_second_counter ++; //Increment 5 second counter
         continue;
      }
      
      DEBUG <<"selectReturned = " << selectReturned << ENDL;

      //Creat sockaddr_in structure to store the ip address the message was recieved (We don't use it but we could if we wanted to)
      struct sockaddr_in recv_addr = {};
      socklen_t addr_len = sizeof(recv_addr);

      //Confirm that select returned becasuse our FD is set, not because of a timeout.
      if(FD_ISSET(recvFd, &readFdSet)) {
        DEBUG << "Bit #" << recvFd << " is set" << ENDL;

        //Call recvfrom()
        if (recvfrom(recvFd, receive_array, 64, 0, (struct sockaddr *)&recv_addr, &addr_len) < 0) {
          perror("recvfrom");
          return -1;
        }

        recv_icmp_header = (struct icmphdr*)(receive_array +recv_ip_header->ihl * 4); //Find the icmp_header to reat the ICMP response type
        //Check if the response was an ICMP_ECHOREPLY
        if (recv_icmp_header->type == ICMP_ECHOREPLY) {
          std::cout << "Congrats bro you got a reply" << std::endl;
          reply_not_received = false;
          not_done_reading = false;

          std::cout << "Final Respondent Received: " << respondent_ip << std::endl;
        }
        else if (recv_icmp_header->type == ICMP_TIME_EXCEEDED)
        {
          
          //Print out router that returned value
          char respondent_ip[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &recv_addr.sin_addr, respondent_ip, INET_ADDRSTRLEN);
          std::cout << "Respondent IP: " << respondent_ip << std::endl;

          not_done_reading = false;
        }
      }
    }

    //No response print statement
    if (five_second_counter >= 3){
        std::cout << "No response with TTL of: " << current_ttl << std::endl;
    }

    //Increment TTL
    current_ttl += 1;

  }
}