#include "mbed.h"

#include "easy-connect.h"

// buffer sizes for socket-related operations (read/write)
#define SOCKET_SEND_BUFFER_SIZE 32
#define SOCKET_RECEIVE_BUFFER_SIZE 32

// WiFi access point SSID and PASSWORD
#define WIFI_SSID "**INSERT_HERE_ACCESS_POINT_SSID**"
#define WIFI_PASS "**INSERT_HERE_ACCESS_POINT_PASSWORD**"

// host, port of (sample) TCP server/listener
#define TCP_SERVER_ADDRESS "ws.mqtt.it"
#define TCP_SERVER_PORT 8888

static DigitalOut led1(LED1, false);

static InterruptIn btn(BUTTON1);

// reference to "NetworkInterface" object that will provide for network-related operation (connect, read/write, disconnect)
static NetworkInterface* s_network;

// Thread/EventQueue pair that will manage network operations (EventQueue ensures that "atomic" operations will not be interrupted until completion)
static Thread s_thread_manage_network;
static EventQueue s_eq_manage_network;

// set of states for a simple finite-state-machine whose main purpose is to keep network connection "as much open as possible" (automatic reconnect)  
typedef enum _ConnectionState
{
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_CONNECTED
} ConnectionState;

// current state
static ConnectionState s_connectionState=NETWORK_STATE_DISCONNECTED;

// this callback (scheduled every 5 secs) implements network reconnect policy
void event_proc_manage_network_connection()
{
    if(s_connectionState==NETWORK_STATE_CONNECTED) return;

    printf("> Initializing Network...\n");

    // easy-connect library provides for a "all-in-one" easy-connect(log, ssid, pass) function
    s_network = easy_connect(true, WIFI_SSID, WIFI_PASS);
    
    if (!s_network)
    {
        printf("> ...connection FAILED\n");
        return;
    }

    printf("> ...connection SUCCEEDED\n");

    s_connectionState=NETWORK_STATE_CONNECTED;
}

// this callback (scheduled every second) actually implement a request+reply transaction sample
void event_proc_send_and_receive_data(const char* message_type)
{
    if(s_connectionState!=NETWORK_STATE_CONNECTED) return;

    TCPSocket socket;
    nsapi_error_t socket_operation_return_value;

    // step 1/2: request (open, connect, send...close and return on error)
    printf("Sending TCP request to %s:%d...\n", TCP_SERVER_ADDRESS, TCP_SERVER_PORT);

    socket.set_timeout(3000);
    socket.open(s_network);
    
    socket_operation_return_value = socket.connect(TCP_SERVER_ADDRESS, TCP_SERVER_PORT);
    
    if(socket_operation_return_value != 0)
    {
        printf("...error in socket.connect(): %d\n", socket_operation_return_value);
        socket.close();
        s_connectionState=NETWORK_STATE_DISCONNECTED;
        return;
    }

    char sbuffer[SOCKET_SEND_BUFFER_SIZE];
    sprintf(sbuffer, "%s\r", message_type);
    nsapi_size_t size = strlen(sbuffer);

    socket_operation_return_value = 0;
    
    while(size)
    {
        socket_operation_return_value = socket.send(sbuffer + socket_operation_return_value, size);

        if (socket_operation_return_value < 0)
        {
            printf("...error sending data: %d\n", socket_operation_return_value);
            socket.close();
            return;
        }
        else
        {
            size -= socket_operation_return_value;
            printf("...sent:%d bytes\n", socket_operation_return_value);
        }
    }

    // step 2/2: receive reply (receive, close...close and return on error)
    char rbuffer[SOCKET_RECEIVE_BUFFER_SIZE];

    socket_operation_return_value = socket.recv(rbuffer, sizeof rbuffer);
    
    if (socket_operation_return_value < 0)
    {
        printf("...error receiving data: %d\n", socket_operation_return_value);
    }
    else
    {
        // clear CR/LF chars for a cleaner debug terminal output
        rbuffer[socket_operation_return_value]='\0';
        if(rbuffer[socket_operation_return_value-1]=='\n' || rbuffer[socket_operation_return_value-1]=='\r') rbuffer[socket_operation_return_value-1]='\0';
        if(rbuffer[socket_operation_return_value-2]=='\n' || rbuffer[socket_operation_return_value-2]=='\r') rbuffer[socket_operation_return_value-2]='\0';

        printf("...received: '%s'\n", rbuffer);
    }

    socket.close();

    // id led is toggling everything is working as expected
    led1.write(!led1.read());
}

// in case of an hardware interrupt, the isr routine schedules (on EventQueue dedicated to network operations)
// a call to event_proc_send_and_receive_data(), but with an argument ("btn") different from one used in periodic request+reply ("test", see below)
void btn_interrupt_handler()
{
    s_eq_manage_network.call(event_proc_send_and_receive_data, "btn");
}

int main()
{
    s_eq_manage_network.call_every(5000, event_proc_manage_network_connection);
    s_eq_manage_network.call_every(1000, event_proc_send_and_receive_data, "test");

    btn.fall(&btn_interrupt_handler);

    s_thread_manage_network.start(callback(&s_eq_manage_network, &EventQueue::dispatch_forever));
}
