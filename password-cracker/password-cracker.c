#define _GNU_SOURCE
#include <math.h>
#include <openssl/md5.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_USERNAME_LENGTH 64
#define PASSWORD_LENGTH 6

// global variable to track the number of cracked passwords
int crack_cnt = 0;

/************************* Part A *************************/
// It changes the input num to 26-based number to be added to "aaaaaa"
void conversion_helpler(int num, int* arr) {
  int val = 0;
  for (int i = 0; i < PASSWORD_LENGTH; i++) {
    arr[i] = (num - val) / pow(26, (PASSWORD_LENGTH - 1 - i));
    val += arr[i] * pow(26, (PASSWORD_LENGTH - 1 - i));
  }
}

/**
 * Find a six character lower-case alphabetic password that hashes
 * to the given hash value. Complete this function for part A of the lab.
 *
 * \param input_hash  An array of MD5_DIGEST_LENGTH bytes that holds the hash of a password
 * \param output      A pointer to memory with space for a six character password + '\0'
 * \returns           0 if the password was cracked. -1 otherwise.
 */
int crack_single_password(uint8_t* input_hash, char* output) {
  for (int i = 0; i < (pow(26, PASSWORD_LENGTH)); i++) {
    int arr_plus[PASSWORD_LENGTH] = {0};
    conversion_helpler(i, arr_plus);
    char candidate_passwd[] = "aaaaaa";  //< This variable holds the password we are trying
    for (int j = 0; j < PASSWORD_LENGTH; j++) {
      candidate_passwd[j] += arr_plus[j];
    }
    // Take our candidate password and hash it using MD5
    uint8_t candidate_hash[MD5_DIGEST_LENGTH];  //< This holds the hash of the candidate password
    MD5((unsigned char*)candidate_passwd, strlen(candidate_passwd), candidate_hash);

    // Now check if the hash of the candidate password matches the input hash
    if (memcmp(input_hash, candidate_hash, MD5_DIGEST_LENGTH) == 0) {
      // Match! Copy the password to the output and return 0 (success)
      strncpy(output, candidate_passwd, PASSWORD_LENGTH + 1);
      return 0;
    } else {
      // No match. continue
      continue;
    }
  }
  return -1;
}

/********************* Parts B & C ************************/

/**
 * This struct is the root of the data structure that will hold users and hashed passwords.
 * This could be any type of data structure you choose: list, array, tree, hash table, etc.
 * Implement this data structure for part B of the lab.
 */
// Linked list to store individual user information.
typedef struct linked_list {
  char* user_name;
  uint8_t* hash_password;
  struct linked_list* next;
} linked_list_t;

// store the header of the linked list of all users' information
typedef struct password_set {
  linked_list_t* header;
} password_set_t;

// arguments to be passed into the thread function
typedef struct {
  int start;
  int end;
  password_set_t* candidates;
} thread_args;

/**
 * Initialize a password set.
 * Complete this implementation for part B of the lab.
 *
 * \param passwords  A pointer to allocated memory that will hold a password set
 */
void init_password_set(password_set_t* passwords) {
  // Initialize any fields you add to your password set structure
  passwords->header = NULL;
}

/**
 * Add a password to a password set
 * Complete this implementation for part B of the lab.
 *
 * \param passwords   A pointer to a password set initialized with the function above.
 * \param username    The name of the user being added. The memory that holds this string's
 *                    characters will be reused, so if you keep a copy you must duplicate the
 *                    string. I recommend calling strdup().
 * \param password_hash   An array of MD5_DIGEST_LENGTH bytes that holds the hash of this user's
 *                        password. The memory that holds this array will be reused, so you must
 *                        make a copy of this value if you retain it in your data structure.
 */
void add_password(password_set_t* passwords, char* username, uint8_t* password_hash) {
  // Add the provided user and password hash to a new linkedin list, and append the list to the
  // front of the passwords header.
  linked_list_t* new_list = malloc(sizeof(linked_list_t));
  new_list->user_name = strdup(username);

  uint8_t* password_hash_temp = malloc(MD5_DIGEST_LENGTH);
  memcpy(password_hash_temp, password_hash, MD5_DIGEST_LENGTH);
  new_list->hash_password = password_hash_temp;
  new_list->next = passwords->header;

  passwords->header = new_list;
}

// My thread function checks the numbers from start to end (to be added to "aaaaaa") and see if any
// user has a matching password.
void* mythread(void* arg) {
  thread_args* args = (thread_args*)arg;

  for (int i = args->start; i <= args->end; i++) {
    int arr_plus[PASSWORD_LENGTH] = {0};
    conversion_helpler(i, arr_plus);
    char candidate_passwd[] = "aaaaaa";  //< This variable holds the password we are trying
    for (int j = 0; j < PASSWORD_LENGTH; j++) {
      candidate_passwd[j] += arr_plus[j];
    }
    // Take our candidate password and hash it using MD5
    uint8_t candidate_hash[MD5_DIGEST_LENGTH];  //< This holds the hash of the candidate password
    MD5((unsigned char*)candidate_passwd, strlen(candidate_passwd), candidate_hash);

    password_set_t* temp = args->candidates;
    // use cur to traverse the whole list to see if there is a match
    linked_list_t* cur = temp->header;

    while (cur != NULL) {
      // Now check if the hash of the candidate password matches the input hash
      if (memcmp(cur->hash_password, candidate_hash, MD5_DIGEST_LENGTH) == 0) {
        // Match! print it out and increase crack_cnt by 1
        printf("%s %s\n", cur->user_name, candidate_passwd);
        crack_cnt += 1;
        break;
      } else {
        // No match. continue to the next user
        cur = cur->next;
      }
    }
  }
  return 0;
}

// free the memory allocated.
void linkedlist_clean(linked_list_t* header) {
  while (header != NULL) {
    linked_list_t* temp = header->next;
    free(header->hash_password);
    free(header->user_name);
    free(header);
    header = temp;
  }
}
/**
 * Crack all of the passwords in a set of passwords. The function should print the username
 * and cracked password for each user listed in passwords, separated by a space character.
 * Complete this implementation for part B of the lab.
 *
 * \returns The number of passwords cracked in the list
 */
int crack_password_list(password_set_t* passwords) {
  // we have four threads, and each thread should check 1/4 of the total number of passwords.
  int thread_load = pow(26, PASSWORD_LENGTH) / 4;

  // create fourse threads and its arguments
  pthread_t threads[4];
  thread_args args[4];

  // each thread start from i*thread_load and finish at (i+1)* thread_load - 1, and the last thread
  // should cover everything.
  for (int i = 0; i < 4; i++) {
    int start = i * thread_load;
    int end;

    if (i != (4 - 1)) {
      end = (i + 1) * thread_load - 1;
    } else {
      end = pow(26, PASSWORD_LENGTH) - 1;
    }
    args[i].start = start;
    args[i].end = end;
    args[i].candidates = passwords;

    // create the threads
    if (pthread_create(&threads[i], NULL, mythread, &args[i])) {
      perror("pthread_create failed");
      exit(EXIT_FAILURE);
    }
  }

  // join the threads
  for (int i = 0; i < 4; i++) {
    if (pthread_join(threads[i], NULL)) {
      perror("pthread_join failed");
      exit(EXIT_FAILURE);
    }
  }

  return crack_cnt;
}

/******************** Provided Code ***********************/

/**
 * Convert a string representation of an MD5 hash to a sequence
 * of bytes. The input md5_string must be 32 characters long, and
 * the output buffer bytes must have room for MD5_DIGEST_LENGTH
 * bytes.
 *
 * \param md5_string  The md5 string representation
 * \param bytes       The destination buffer for the converted md5 hash
 * \returns           0 on success, -1 otherwise
 */
int md5_string_to_bytes(const char* md5_string, uint8_t* bytes) {
  // Check for a valid MD5 string
  if (strlen(md5_string) != 2 * MD5_DIGEST_LENGTH) return -1;

  // Start our "cursor" at the start of the string
  const char* pos = md5_string;

  // Loop until we've read enough bytes
  for (size_t i = 0; i < MD5_DIGEST_LENGTH; i++) {
    // Read one byte (two characters)
    int rc = sscanf(pos, "%2hhx", &bytes[i]);
    if (rc != 1) return -1;
    pos += 2;
  }
  return 0;
}

void print_usage(const char* exec_name) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  %s single <MD5 hash>\n", exec_name);
  fprintf(stderr, "  %s list <password file name>\n", exec_name);
}

int main(int argc, char** argv) {
  if (argc != 3) {
    print_usage(argv[0]);
    exit(1);
  }

  if (strcmp(argv[1], "single") == 0) {
    // The input MD5 hash is a string in hexadecimal. Convert it to bytes.
    uint8_t input_hash[MD5_DIGEST_LENGTH];
    if (md5_string_to_bytes(argv[2], input_hash)) {
      fprintf(stderr, "Input has value %s is not a valid MD5 hash.\n", argv[2]);
      exit(1);
    }

    // Now call the crack_single_password function
    char result[7];
    if (crack_single_password(input_hash, result)) {
      printf("No matching password found.\n");
    } else {
      printf("%s\n", result);
    }

  } else if (strcmp(argv[1], "list") == 0) {
    // Make and initialize a password set
    password_set_t passwords;
    init_password_set(&passwords);

    // Open the password file
    FILE* password_file = fopen(argv[2], "r");
    if (password_file == NULL) {
      perror("opening password file");
      exit(2);
    }

    int password_count = 0;

    // Read until we hit the end of the file
    while (!feof(password_file)) {
      // Make space to hold the username
      char username[MAX_USERNAME_LENGTH];

      // Make space to hold the MD5 string
      char md5_string[MD5_DIGEST_LENGTH * 2 + 1];

      // Make space to hold the MD5 bytes
      uint8_t password_hash[MD5_DIGEST_LENGTH];

      // Try to read. The space in the format string is required to eat the newline
      if (fscanf(password_file, "%s %s ", username, md5_string) != 2) {
        fprintf(stderr, "Error reading password file: malformed line\n");
        exit(2);
      }

      // Convert the MD5 string to MD5 bytes in our new node
      if (md5_string_to_bytes(md5_string, password_hash) != 0) {
        fprintf(stderr, "Error reading MD5\n");
        exit(2);
      }

      // Add the password to the password set
      add_password(&passwords, username, password_hash);
      password_count++;
    }

    // Now run the password list cracker
    int cracked = crack_password_list(&passwords);
    linkedlist_clean(passwords.header);

    printf("Cracked %d of %d passwords.\n", cracked, password_count);

  } else {
    print_usage(argv[0]);
    exit(1);
  }

  return 0;
}
