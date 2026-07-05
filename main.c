#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    char* buffer;
    size_t buffer_length; //size_t is an unsigned int type used specifically for sizes in bytes
    ssize_t input_length; //ssize_t is signed, read functions can return -1 to signal an error
} InputBuffer; 

typedef enum{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult; 

typedef enum{
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

typedef enum{
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType; 

typedef struct{
    StatementType type;
} Statement; 

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

MetaCommandResult do_meta_command(InputBuffer* input_buffer){
    if(strcmp(input_buffer->buffer, ".exit") == 0){
        close_input_buffer(input_buffer);
        exit(EXIT_SUCCESS);
    }
    else{
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement){
    if(strncmp(input_buffer->buffer, "insert", 6) == 0){
        statement->type = STATEMENT_INSERT;
        return PREPARE_SUCCESS;
    }
    if(strncmp(input_buffer->buffer, "select", 6) == 0){
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

void execute_statement(Statement* statement){
    switch(statement->type){
        case(STATEMENT_INSERT):{
            printf("This is where an insert would happen.\n");
            break;
        }
        case(STATEMENT_SELECT):{
            printf("This is where a select would happen.\n");
            break; 
        }
    }
}



int main(int argc, char* argv[]){
    InputBuffer* input_buffer = new_input_buffer();
    while(true){
        print_prompt();
        read_input(input_buffer);

        if(input_buffer->buffer[0] == "."){
            switch(do_meta_command(input_buffer)){
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
            case(PREPARE_UNRECOGNIZED_STATEMENT):{
                printf("Unrecognised keyword at the start fo %s.\n", input_buffer->buffer);
                continue;
            }
        }

        execute_statement(&statement);
        printf("Executed.\n"); 
    }

}
