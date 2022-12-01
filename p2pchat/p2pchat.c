#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "socket.h"
#include "ui.h"

// The header for the function send_message and receive_message
// The header is provided from the networking exercise in CSC-213 by Charlie Curtsinger
#include "message.h"

// Struct for a user
typedef struct user {
  // This field stores the socket fd
  int socket_fd;
  // This field stores the pointer to the next node
  struct user* next;
} user_t;

// The head pointer for the user list
user_t* user_list = NULL;

// A few helper functions for the user list.
// The documentation is with the function definition.
int* insert_user(int socket_fd);
int free_user(int socket_fd);
void free_all_users();
void send_out(const char* username_from, const char* message, int current_socket_fd);

/**
 * The thread function for each user in the user list
 *
 * \param arg  The address of the user socket fd
 * \returns    NULL
 */
void* user_thread_func(void* arg) {

  // The socket fd for the user
  int* user_socket_fd = (int*)arg;

  // Infinite loop to receive infinite messages
  while (1) {

    // Receive the username
    char* username_from = receive_message(*user_socket_fd);
    if (username_from == NULL) {
      // Check an error for receive_message
      // If there is an error, close the socket, delete the user from the user list, and end the thread
      close(*user_socket_fd);
      free_user(*user_socket_fd);
      return NULL;
    }

    // Receive the message
    char* message = receive_message(*user_socket_fd);
    if (message == NULL) {
      // Check an error for receive_message
      // If there is an error, close the socket, delete the user from the user list, and end the thread
      close(*user_socket_fd);
      free_user(*user_socket_fd);
      return NULL;
    }

    // Display the received message
    ui_display(username_from, message);

    // Send out the received message to all the users in the user list except for the sender
    send_out(username_from, message, *user_socket_fd);

    // Free the message
    free(message);
  }

  // The lines below will be never reached due to the above infinite loop. However, they are here just in case.
  // Close the socket, delete the user from the user list, and end the thread
  close(*user_socket_fd);
  free_user(*user_socket_fd);
  return NULL;
}

/**
 * The thread function to avoid the conflict with the UI
 * It creates a new thread whenever a new user is connected to this server.
 *
 * \param arg  The address of the server socket fd
 * \returns    NULL
 */
void* receive_thread_func(void* arg) {

  // The socket fd for the server
  int* server_socket_fd = (int*)arg;

  if (user_list != NULL) {
    // If the server connects to another user, create a new thread for the user
    pthread_t first_user_thread;
    if (pthread_create(&first_user_thread, NULL, user_thread_func, &user_list->socket_fd)) {
      // Check an error foor pthread_create
      perror("pthread_create failed");
      exit(EXIT_FAILURE);
    }
  }

  // Infinite loop for infinite new users
  while (1) {
    // Wait for a new user to connect
    int new_user_socket_fd = server_socket_accept(*server_socket_fd);
    if (new_user_socket_fd == -1) {
      perror("accept failed");
      exit(EXIT_FAILURE);
    }

    // Create a thread for the new user
    pthread_t user_thread;
    int* user_socket_fd = insert_user(new_user_socket_fd);
    if (pthread_create(&user_thread, NULL, user_thread_func, user_socket_fd)) {
      // Check an error for pthread_create
      perror("pthread_create failed");
      exit(EXIT_FAILURE);
    }
  }
}

// Keep the username in a global so we can access it from the callback
const char* username;

// This function is run whenever the user hits enter after typing a message
void input_callback(const char* message) {
  if (strcmp(message, ":quit") == 0 || strcmp(message, ":q") == 0) {
    // If :q or :quit is typed, exit the UI
    ui_exit();
  } else {
    // Otherwise, display the username and the message and send out the message to the all the users in the user list
    ui_display(username, message);
    send_out(username, message, -1);
  }
}

// The main function
int main(int argc, char** argv) {
  // Make sure the arguments include a username
  if (argc != 2 && argc != 4) {
    fprintf(stderr, "Usage: %s <username> [<peer> <port number>]\n", argv[0]);
    exit(1);
  }

  // Save the username in a global
  username = argv[1];

  // Open a server socket
  unsigned short port = 0;
  int server_socket_fd = server_socket_open(&port);
  if (server_socket_fd == -1) {
    // Check an error for server_socket_open
    perror("Server socket was not opened");
    exit(EXIT_FAILURE);
  }

  // Start listening for connections, with a maximum of one queued connection
  if (listen(server_socket_fd, 1)) {
    // Check an error for listen
    perror("listen failed");
    exit(EXIT_FAILURE);
  }

  // If a peer is specified in the command line, connect to the peer.
  if (argc == 4) {
    // Unpack arguments
    char* peer_hostname = argv[2];
    unsigned short peer_port = atoi(argv[3]);

    // Connect to the specified peer
    int socket_fd = socket_connect(peer_hostname, peer_port);
    if (socket_fd == -1) {
      // Check an error for socket_connect
      perror("Failed to socket_connect");
      exit(EXIT_FAILURE);
    }

    // Add the specified peer to the user list
    insert_user(socket_fd);
  }

  // Set up the user interface. The input_callback function will be called each time the user hits enter to send a message.
  ui_init(input_callback);

  // Create a log message that shows the port number
  char buffer[50];
  if (snprintf(buffer, 50, "Server listening on port %u.\n", port) < 0) {
    // Check an error for snprintf
    perror("snprintf failed");
    exit(EXIT_FAILURE);
  }

  // Display the log message
  ui_display("INFO", buffer);

  // Create a thread to avoid the conflict with the UI
  pthread_t receive_thread;
  if (pthread_create(&receive_thread, NULL, receive_thread_func, &server_socket_fd)) {
    perror("pthread_create failed");
    exit(EXIT_FAILURE);
  }

  // Run the UI loop. This function only returns once we call ui_stop() somewhere in the program.
  ui_run();

  // After the UI is stopped, free all the users in the user list
  free_all_users();

  return 0;
}

// A few helper functions for the user list.

// The mutex lock for the user list in the helper functions
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Insert a new user to the user list
 *
 * \param socket_fd  The socket fd of the new user
 * \returns          The address of the socket fd of the new user
 */
int* insert_user(int socket_fd) {
  // Create a new node and stores the socket fd
  user_t* new_node = malloc(sizeof(user_t));
  new_node->socket_fd = socket_fd;

  // Insert the new node at the beginning of the user list.
  // This process is secured by the mutex lock
  pthread_mutex_lock(&lock);
  new_node->next = user_list;
  user_list = new_node;
  pthread_mutex_unlock(&lock);

  // Return the address of the socket fd of the new user
  return &(new_node->socket_fd);
}

/**
 * Free a user in the user list
 *
 * \param socket_fd  The socket fd of the user to be freed
 * \returns           0 if the user is successfully freed
 *                   -1 otherwise. This generally means that the user to be freed is not in the user list
 */
int free_user(int socket_fd) {
  // Check whether the user list is empty
  if (user_list == NULL){
    // If the user list is empty, return -1
    return -1;
  }

  // Find the user to be free and free it if there exists.
  // This process is secured by the mutex lock
  pthread_mutex_lock(&lock);

  // Check whether the first user is the user to be freed
  if (user_list->socket_fd == socket_fd){
    // If so, free it and return 0
    user_t* next_node = user_list->next;
    free(user_list);
    user_list = next_node;
    pthread_mutex_unlock(&lock);
    return 0;
  }
  
  // Traverse the user list, starting from the second user
  user_t* temp_node = user_list;
  while(temp_node->next != NULL){
    // Check whether the current user is the user to be freed
    if (temp_node->next->socket_fd == socket_fd){
      // If so, free it and return 0
      user_t* next_node = temp_node->next->next;
      free(temp_node->next);
      temp_node->next = next_node;
      pthread_mutex_unlock(&lock);
      return 0;
    }

    // Move to the next user
    temp_node = temp_node->next;
  }

  // If the user to be freed does not exist return -1
  pthread_mutex_unlock(&lock);
  return -1;
}

/**
 * Free all the users in the user list
 */
void free_all_users() {
  // Free all the users in the user list
  // This process is secured by the mutext lock
  pthread_mutex_lock(&lock);

  // Traverse the user list and free each user
  while (user_list != NULL) {
    user_t* next_node = user_list->next;
    free(user_list);
    user_list = next_node;
  }
  pthread_mutex_unlock(&lock);
}

/**
 * Send out the message to all the users in the user list except for the specified user
 *
 * \param sender           the username of the user who sent the message
 * \param message          the message
 * \param sender_socket_fd the socket fd of the user who sent the message
 *                         -1 indicates that the sender is this server
 */
void send_out(const char* sender, const char* message, int sender_socket_fd) {
  // Send out the message to all the users in the user list except for the specified user
  // This process is secured by the mutex lock
  pthread_mutex_lock(&lock);

  // Traverse the user list and send the sender's username and the message to each user
  user_t* temp_node = user_list;
  while (temp_node != NULL) {
    if (temp_node->socket_fd != sender_socket_fd) {
      if(send_message(temp_node->socket_fd, sender) == -1){
        // Check an error for send_message
        perror("send_message failed");
        exit(EXIT_FAILURE);
      }
      if(send_message(temp_node->socket_fd, message) == -1){
        // Check an error for send_message
        perror("send_meesage failed");
        exit(EXIT_FAILURE);
      }
    }

    // Move to the next user
    temp_node = temp_node->next;
  }
  
  pthread_mutex_unlock(&lock);
}