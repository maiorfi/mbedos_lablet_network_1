#include "mbed.h"

#include "easy-connect.h"

#define SOCKET_SEND_BUFFER_SIZE 32
#define SOCKET_RECEIVE_BUFFER_SIZE 32

#define WIFI_SSID "********"
#define WIFI_PASS "********"
#define TCP_SERVER_ADDRESS "ws.mqtt.it"
#define TCP_SERVER_PORT 8888

static DigitalOut led1(LED1, false);

static InterruptIn btn(BUTTON1);

static NetworkInterface* s_network;

static Thread s_thread_manage_network;
static EventQueue s_eq_manage_network;

typedef enum _ConnectionState
{
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_CONNECTED
} ConnectionState;

static ConnectionState s_connectionState=NETWORK_STATE_DISCONNECTED;

void event_proc_manage_network_connection()
{
    if(s_connectionState==NETWORK_STATE_CONNECTED) return;

    printf("> Initializing Network...\n");

    s_network = easy_connect(true, WIFI_SSID, WIFI_PASS);
    
    if (!s_network)
    {
        printf("> ...connection FAILED\n");
        return;
    }

    printf("> ...connection SUCCEEDED\n");

    s_connectionState=NETWORK_STATE_CONNECTED;
}

void event_proc_send_and_receive_data(const char* message_type)
{
    if(s_connectionState!=NETWORK_STATE_CONNECTED) return;

    TCPSocket socket;
    nsapi_error_t response;

    printf("Sending TCP request to %s:%d...\n", TCP_SERVER_ADDRESS, TCP_SERVER_PORT);

    socket.set_timeout(3000);
    socket.open(s_network);
    
    response = socket.connect(TCP_SERVER_ADDRESS, TCP_SERVER_PORT);
    
    if(response != 0)
    {
        printf("...error in socket.connect(): %d\n", response);
        socket.close();
        s_connectionState=NETWORK_STATE_DISCONNECTED;
        return;
    }

    char sbuffer[SOCKET_SEND_BUFFER_SIZE];
    sprintf(sbuffer, "%s\r", message_type);
    nsapi_size_t size = strlen(sbuffer);

    response = 0;
    
    while(size)
    {
        response = socket.send(sbuffer+response, size);

        if (response < 0)
        {
            printf("...error sending data: %d\n", response);
            socket.close();
            return;
        }
        else
        {
            size -= response;
            printf("...sent:%d bytes\n", response);
        }
    }

    char rbuffer[SOCKET_RECEIVE_BUFFER_SIZE];

    response = socket.recv(rbuffer, sizeof rbuffer);
    
    if (response < 0)
    {
        printf("...error receiving data: %d\n", response);
    }
    else
    {
        rbuffer[response]='\0';
        if(rbuffer[response-1]=='\n' || rbuffer[response-1]=='\r') rbuffer[response-1]='\0';
        if(rbuffer[response-2]=='\n' || rbuffer[response-2]=='\r') rbuffer[response-2]='\0';

        printf("...received: '%s'\n",rbuffer);
    }

    socket.close();

    led1.write(!led1.read());
}

void event_proc_btn_handler()
{
    s_eq_manage_network.call(event_proc_send_and_receive_data, "btn");
}

void btn_interrupt_handler()
{
    s_eq_manage_network.call(&event_proc_btn_handler);
}

int main()
{
    s_eq_manage_network.call_every(5000, event_proc_manage_network_connection);
    s_eq_manage_network.call_every(1000, event_proc_send_and_receive_data, "test");

    btn.fall(&btn_interrupt_handler);

    s_thread_manage_network.start(callback(&s_eq_manage_network, &EventQueue::dispatch_forever));
}
