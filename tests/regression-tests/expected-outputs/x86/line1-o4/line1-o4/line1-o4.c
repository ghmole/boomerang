int main(union { int; char *x1; FILE *; } argc, char *argv[]);


/** address: 0x08048450 */
int main(union { int; char *x1; FILE *; } argc, char *argv[])
{
    int eax_1; 		// r24
    char *eax_11; 		// r24{13}
    union { int; char *x1; FILE *; } eax_2; 		// r24{8}
    int eax_5; 		// r24{9}
    union { int; char *; FILE *x7; } eax_8; 		// r24{11}
    int edx; 		// r26
    char local0; 		// m[esp - 0x40c]
    int local5; 		// eax_1{17}

    if (argc <= 1) {
        eax_1 = 1;
        local5 = eax_1;
    }
    else {
        edx = *(argv + 4);
        eax_2 = fopen(edx, "r");
        eax_5 = 1;
        local5 = eax_5;
        if (eax_2 != 0) {
            eax_8 = fgets(&local0, 1024, eax_2);
            if (eax_8 != 0) {
                eax_11 = strchr(eax_8, 10);
                if (eax_11 != 0) {
                    *(char*)eax_11 = 0;
                }
                puts(&local0);
            }
            eax_1 = fclose(eax_2);
            local5 = eax_1;
        }
    }
    eax_1 = local5;
    return eax_1;
}

