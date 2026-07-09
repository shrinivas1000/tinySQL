#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

typedef struct {
    char* buffer;
    size_t buffer_length; //size_t is an unsigned int type used specifically for sizes in bytes
    ssize_t input_length; //ssize_t is signed, read functions can return -1 to signal an error
} InputBuffer; 

typedef struct{
    uint32_t id; //unsigned 32 bit integer
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1]; 
} Row;


typedef enum{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult; 

typedef enum{
    PREPARE_SUCCESS,
    PREPARE_NEGATIVE_ID,
    PREPARE_STRING_TOO_LONG, 
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

typedef enum{
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult; 

typedef enum{
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType; 

typedef struct{
    StatementType type;
    Row row_to_insert; 
} Statement; 

#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

/*
(Struct*)0 -> (Struct*)0 this tells the C compiler to pretend that a valid struct of your chosen type 
(e.g., Row) is currently sitting at memory address 0 

sizeof() is evaluated entirely by the compiler before the program even starts 
The compiler just calculates the theoretical memory footprint 
*/

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096;
#define TABLE_MAX_PAGES 100
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

typedef struct{
    int file_descriptor;
    uint32_t file_length;
    void* pages[TABLE_MAX_PAGES];
} Pager;

typedef struct {
  uint32_t num_rows;
  Pager* pager; 
} Table;

typedef struct {
    Table* table;
    uint32_t row_num; 
    bool end_of_table; //position one past the last ele
} Cursor; 

// to track if node is internal or leaf node in B+ tree
typedef enum { NODE_INTERNAL, NODE_LEAF} NodeType; 

// this is the metadata for an internal(common) node header layout
//first 6 bytes of any node will have this "metadata"
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0; 
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE; //this is reqd for node splitting! 
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t); 
const uint32_t COMMON_NODE_HEADER_SIZE = NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE; 

// leaf node header layout
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE; // 6+4 = 10 bytes

// leaf Node Body Layout -> since a leaf node will actually store data
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE; //4096 - 10 = 4086 bytes
const uint32_t LEAF_NODE_MAX_CELLS =
    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;

Cursor table_start(Table* table){
    Cursor cursor; 
    cursor.table = table; 
    cursor.row_num = 0; 
    cursor.end_of_table = (table->num_rows == 0);
    return cursor; 
}

Cursor table_end(Table* table) {
  Cursor cursor;
  cursor.table = table;
  cursor.row_num = table->num_rows;
  cursor.end_of_table = true;
  return cursor;
}


//void *memcpy(void *dest, const void *src, size_t n);
//cast destination to (char*) because standard C does not allow pointer arithmetic on a raw void*

void serialize_row(Row* source, void* destination) {
  memcpy((char*)destination + ID_OFFSET, &(source->id), ID_SIZE);
  memcpy((char*)destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
  memcpy((char*)destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void deserialize_row(void* source, Row* destination) {
  memcpy(&(destination->id), (char*)source + ID_OFFSET, ID_SIZE);
  memcpy(&(destination->username), (char*)source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(destination->email), (char*)source + EMAIL_OFFSET, EMAIL_SIZE);
}


Pager* pager_open(const char* filename){
    int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
/*O_RDWR | O_CREAT tells the os kernel to open the file for reading and writing, and to create it if it does not exist
 S_IWUSR | S_IRUSR sets the file permissions so the user can read and write to it.*/
    if(fd == -1){
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }

    off_t file_length = lseek(fd, 0, SEEK_END);

    Pager* pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd; 
    pager->file_length = file_length; 

    for(uint32_t i = 0; i < TABLE_MAX_PAGES; i++){
        pager->pages[i] = NULL; 
    }

    return pager;
}

Table* db_open(const char* filename){   
    Pager* pager = pager_open(filename); 
    uint32_t num_rows = pager->file_length / ROW_SIZE; 

    Table* table = malloc(sizeof(Table));
    table->pager = pager;
    table->num_rows = num_rows; 

    return table; 
}

void* get_page(Pager* pager, uint32_t page_num){
    if(page_num >= TABLE_MAX_PAGES){
        printf("Tried to fetch page number out of bounds. %d > %d\n", page_num, TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }
    
    if(pager->pages[page_num] == NULL){
        //cache miss! allocate memory and load from file
        void* page = malloc(PAGE_SIZE); 
        uint32_t num_pages = pager->file_length / PAGE_SIZE; 

        if(pager->file_length % PAGE_SIZE){
            num_pages += 1; 
        }

        if(page_num <= num_pages){
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE); 
            if(bytes_read == -1){
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE); 
            }
        }
        pager->pages[page_num] = page; 
    }
    return pager->pages[page_num]; 
}

void pager_flush(Pager* pager, uint32_t page_num, uint32_t size){
    if(pager->pages[page_num] == NULL){
        printf("Tried to flush null page\n"); 
        exit(EXIT_FAILURE);
    }
    off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
    if(offset == -1){
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    ssize_t bytes_written = write(pager->file_descriptor, pager->pages[page_num], size);
    if(bytes_written == -1){
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

void db_close(Table* table){
    Pager* pager = table->pager;
    uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE;

    for(uint32_t i = 0; i < num_full_pages; i++){
        if(pager->pages[i] == NULL){
            continue; 
        }
        pager_flush(pager, i, PAGE_SIZE);
        free(pager->pages[i]); 
        pager->pages[i] = NULL; 
    }
    uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
    if (num_additional_rows > 0) {

        uint32_t page_num = num_full_pages;

        if (pager->pages[page_num] != NULL) {

        pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
        free(pager->pages[page_num]);
        pager->pages[page_num] = NULL;
        }
    }

    int result = close(pager->file_descriptor);
    if (result == -1) {
        printf("Error closing db file.\n");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        void* page = pager->pages[i];
        if (page) {
        free(page);
        pager->pages[i] = NULL; 
        }
    }
    free(pager);
    free(table);
}

void print_row(Row* row){
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

void* cursor_value(Cursor* cursor){
    uint32_t row_num = cursor->row_num;
    uint32_t page_num = row_num / ROWS_PER_PAGE;
    void* page = get_page(cursor->table->pager, page_num);
    uint32_t row_offset = row_num % ROW_SIZE;
    uint32_t byte_offset = row_offset * ROW_SIZE;
    return (char*)page + byte_offset; 
}

void cursor_next(Cursor* cursor){
    cursor->row_num += 1; 
    if(cursor->row_num >= cursor->table->num_rows){
        cursor->end_of_table = true; 
    }
}


ExecuteResult execute_insert(Statement* statement, Table* table){
    if(table->num_rows >= TABLE_MAX_ROWS){
        return EXECUTE_TABLE_FULL; 
    }
    Row* row_to_insert = &(statement->row_to_insert);
    Cursor cursor = table_end(table); //im opening a cursor at the insertion pt

    serialize_row(row_to_insert, cursor_value(&cursor));

    table->num_rows += 1;
    return EXECUTE_SUCCESS; 
}

ExecuteResult execute_select(Statement* statement, Table* table){
    Cursor cursor = table_start(table); //open cursor at the first row
    Row row; 
    while(!cursor.end_of_table){
        deserialize_row(cursor_value(&cursor), &row);
        print_row(&row);
        cursor_next(&cursor); //move cursor forward 
    }
    return EXECUTE_SUCCESS; 
}

ExecuteResult execute_statement(Statement* statement, Table* table) {
  switch (statement->type) {
    case (STATEMENT_INSERT):
      return execute_insert(statement, table);
    case (STATEMENT_SELECT):
      return execute_select(statement, table);
  }
}

InputBuffer* new_input_buffer(){
    InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
    // clean the malloc
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0; 

    return input_buffer;
}

void print_prompt(){
    printf("db > ");
}

void read_input(InputBuffer* input_buffer){
    ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

    if(bytes_read <= 0){
        printf("Error reading input\n");
        exit(EXIT_FAILURE); //EXIT_FAILURE is defined in stdlib.h btw
    }

    input_buffer->input_length = bytes_read - 1;
    input_buffer->buffer[bytes_read - 1] = 0; //null terminate the string with 0 
}

void close_input_buffer(InputBuffer* input_buffer){

    free(input_buffer->buffer);
    free(input_buffer); 
}

MetaCommandResult do_meta_command(InputBuffer* input_buffer,Table* table){
    if(strcmp(input_buffer->buffer, ".exit") == 0){
        close_input_buffer(input_buffer);
        db_close(table); 
        exit(EXIT_SUCCESS);
    }
    else{
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* statement){
    statement->type = STATEMENT_INSERT;

    char* keyword = strtok(input_buffer->buffer, " ");
    char* id_string = strtok(NULL, " ");
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, " ");

    if (id_string == NULL || username == NULL || email == NULL){
        return PREPARE_SYNTAX_ERROR;
    }

    int id = atoi(id_string); //string to int conversion 
    if(id < 0){
        return PREPARE_NEGATIVE_ID; 
    }
    if(strlen(username) > COLUMN_USERNAME_SIZE){
        return PREPARE_STRING_TOO_LONG; 
    }
    if (strlen(email) > COLUMN_EMAIL_SIZE) {
    return PREPARE_STRING_TOO_LONG;
    }
    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARE_SUCCESS; 
}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement){
    if(strncmp(input_buffer->buffer, "insert", 6) == 0){
        return prepare_insert(input_buffer, statement); 
    }
    if(strncmp(input_buffer->buffer, "select", 6) == 0){
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

int main(int argc, char* argv[]){
    InputBuffer* input_buffer = new_input_buffer();
    if(argc < 2){
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    char* filename = argv[1];
    Table* table = db_open(filename);

    while(true){
        print_prompt();
        read_input(input_buffer);

        if(input_buffer->buffer[0] == '.'){
            switch(do_meta_command(input_buffer, table)){
                case(META_COMMAND_SUCCESS):{
                    continue;
                }
                case(META_COMMAND_UNRECOGNIZED_COMMAND):{
                    printf("Unrecognised command '%s'\n", input_buffer->buffer);
                    continue; //continue can only be used for loops
                }
            }
        }

        Statement statement;

        switch(prepare_statement(input_buffer, &statement)){
            case(PREPARE_SUCCESS):{
                break; //this break applies to the switch, continue applies to the while loop 
            }
            case(PREPARE_NEGATIVE_ID):{
                printf("ID must be positive.\n"); 
                continue; 
            }
            case(PREPARE_STRING_TOO_LONG):{
                printf("String is too long.\n"); 
                continue; 
            }
            case(PREPARE_SYNTAX_ERROR):{
                printf("Syntax error. Could not parse statement.\n"); 
                continue; 
            }
            case(PREPARE_UNRECOGNIZED_STATEMENT):{
                printf("Unrecognised keyword at the start fo %s.\n", input_buffer->buffer);
                continue;
            }
        }

        switch (execute_statement(&statement, table)){
            case(EXECUTE_SUCCESS):{
                printf("Executed.\n");
                break; 
            }
            case(EXECUTE_TABLE_FULL):{
                printf("Error: Table full.\n");
                break; 
            }

        }
        
    }

}
