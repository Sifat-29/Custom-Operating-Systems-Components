#include "loader.h"

char* isValidElf(Elf32_Ehdr* ehdr) {
    if (ehdr->e_ident[EI_MAG0] != 0x7f || ehdr->e_ident[EI_MAG1] != 'E' || ehdr->e_ident[EI_MAG2] != 'L' || ehdr->e_ident[EI_MAG3] != 'F') {
        return "Incorrect Magic numbers, not an elf";
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        return "Not a 32-bit elf";
    }

    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return "Not an elf with LSB data orientation";
    }
    
    if (ehdr->e_type != ET_EXEC) {
        return "Not an executable elf";
    }

    return NULL;
}

int main(int argc, char** argv) {
    if (argc != 4 || argv[0] == NULL || argv[1] == NULL || argv[2] == NULL || argv[3] == NULL) {
        printf("Usage: %s <ELF Executable> <Page Replacement Policy ({FIFO}, {RANDOM})> <Max Number of Pages>\n", argv[0]);
        exit(1);
    }

    // 1. carry out necessary checks on the input ELF file
	// Initialising global struct for elf header
	Elf32_Ehdr *ehdr = (Elf32_Ehdr*) malloc(sizeof(Elf32_Ehdr));
    if (ehdr == NULL) {printf("Unable to allocate memory for header file\n"); exit(1);}
	
	// Opening file to read elf header to check validity
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {printf("Unable to open file for initial checking of elf\n"); exit(1);}

	// reading elf header and terminating in case of error
    ssize_t read_bytes_header = read(fd, ehdr, sizeof(Elf32_Ehdr));
    if (read_bytes_header != sizeof(Elf32_Ehdr)) {
        printf("Unable to load header, bytes read = %zd, expected %u\n", read_bytes_header, sizeof(Elf32_Ehdr));
        exit(1);
    }

    close(fd);

	// Performing checks on the elf header
	char* validity = isValidElf(ehdr);
    if (validity != NULL) {printf("%s\n", validity); exit(1);}

    // 2. passing it to the loader for carrying out the loading/execution
    load_and_run_elf(argv);
    // 3. invoke the cleanup routine inside the loader  
    loader_cleanup();
    
    return 0;
}