#pragma once

#include <queue>
#include "packet.hpp"

class Client
{
public:
    Client(int socket_fd);

    void send_packet(Packet *packet);
    void *handle_connection();

    std::queue<Packet *> *get_incomming_packet_queue()
    {
        return &this->incomming_packet_queue;
    }

    std::queue<Packet *> *get_outgoing_packet_queue()
    {
        return &this->outgoing_packet_queue;
    }

    int get_socket_fd()
    {
        return this->socket_fd;
    }

private:
    int socket_fd;
    std::queue<Packet *> incomming_packet_queue;
    std::queue<Packet *> outgoing_packet_queue;

    void handle_packet(Packet *packet);
};