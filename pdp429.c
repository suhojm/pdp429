/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

FILE *input;

typedef short Boolean;
#define TRUE 1
#define FALSE 0

Boolean debug = FALSE;
Boolean verbose = FALSE;
Boolean ASCII = TRUE;

typedef char *STRING;

#define CAST(t,e) ((t)(e))
#define TYPED_MALLOC(t) CAST(t*, malloc(sizeof(t)))


/* ***************************************************************** */
/*                                                                   */
/* print representation of a character for debugging                 */
/*                                                                   */
char   *printrep (unsigned int  c)
{
    static char pr[8];

    if (c < 32)
        {
            /* control characters */
            pr[0] = '^';
            pr[1] = c + 64;
            pr[2] = '\0';
        }
    else if (c < 127)
        {
            /* printing characters */
            pr[0] = c;
            pr[1] = '\0';
        }
    else if (c == 127)
        return("<del>");
    else if (c <= 0377)
        {
            /* upper 128 codes from 128 to 255;  print as \ooo - octal  */
            pr[0] = '\\';
            pr[3] = '0' + (c & 7);
            c = c >> 3;
            pr[2] = '0' + (c & 7);
            c = c >> 3;
            pr[1] = '0' + (c & 3);
            pr[4] = '\0';
        }
    else
        {
            /* very large number -- print as 0xffff - 4 digit hex */
            (void)sprintf(pr, "0x%04x", c);
        }
    return(pr);
}


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

/* MALLOC space for a string and copy it */

STRING remember_string(const STRING name)
{
    size_t n;
    STRING p;

    if (name == NULL) return(NULL);

    /* get memory to remember file name */
    n = strlen(name) + 1;
    p = CAST(STRING, malloc(n));
    strcpy(p, name);
    return(p);
}


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

char line[10];
int line_length = 8;

int get_next_line(void)
{
    /* get the first character to see if we have EOF */
    int c;
    int i = 0;
    c = getc(input);
    if (c != EOF)
        {
            line[i] = c;
            i = 1;
            while (((c = getc(input)) != EOF) && (c != '\n'))
                {
                    if (i < line_length)
                        {
                            line[i] = c;
                            i = i + 1;
                        }
                }
        }
    line[i] = '\0';

    if (debug) fprintf(stderr, "next input line: %s\n", line);

    if ((c == EOF) && (i == 0))
        return(EOF);
    else
        return(i);
}



/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

Boolean is_hex(char c)
{
    if (('0' <= c) && (c <= '9')) return(TRUE);
    if (('A' <= c) && (c <= 'F')) return(TRUE);
    if (('a' <= c) && (c <= 'f')) return(TRUE);
    return(FALSE);
}

int hex_char_value(char c)
{
    if (('0' <= c) && (c <= '9')) return(c-'0');
    if (('A' <= c) && (c <= 'F')) return(c-'A' + 10);
    if (('a' <= c) && (c <= 'f')) return(c-'a' + 10);
    return(-1);
}

int hex_value(char *p)
{
    int n = 0;
    while (is_hex(*p))
        {
            n = n * 16 + hex_char_value(*p);
            p++;
        }
    return(n);
}


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

/* PDP-8 types */

typedef int Address;
typedef int INST;
typedef int Word;


#define MASK_W16      0xFFFF
#define MASK_UPPER6   0xFF00
#define MASK_LOWER6   0x00FF
#define MASK_SIGN_BIT 0x8000


#define MASK_I_BIT   0x0200
#define MASK_ZC_BIT  0x0100
#define MASK_OFFSET  0x00FF

#define MASK_OVERFLOW 0xFFFF0000    /* Any bit in high-order 4 bits is overflow */

#define MASK_SM 0x0200
#define MASK_SZ 0x0100
#define MASK_SNL 0x0080
#define MASK_RSS 0x0040
#define MASK_CL 0x0020
#define MASK_CLL 0x0010
#define MASK_CM 0x0008
#define MASK_CML 0x0004
#define MASK_DC 0x0002
#define MASK_IN 0x0001

#define MASK_IOT_DEVICE   0x03F8
#define MASK_IOT_FUNCTION 0x0007


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

INST     memory[65536];
Boolean defined[65536];


void Clear_Memory(void)
{
    int i;
    for (i = 0; i < 65536; i++)
        {
            defined[i] = FALSE;
        }
}

void Store_Memory(Address addr, Word value)
{
    if (debug)
        fprintf(stderr, "write memory: 0x%03X = 0x%03X\n", addr, value);
    defined[addr] = TRUE;
    memory[addr] = value & MASK_W16;
}

INST Fetch_Memory(Address addr)
{
    Word value;

    if (defined[addr])
        value = memory[addr];
    else
        value = 0;

    if (debug)
        fprintf(stderr, "read memory: 0x%03X = 0x%03X\n", addr, value);
    return(value);
}



/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

/* registers */
Address PC = 0;
Word PSW = 1;
Word SP = 0xFFFF;
Word SPL;
Word A = 0;
Word B = 0;
Word C = 0;
Word D = 0;
Word L = 0;
char regName[5];
char INDSTRING[100];
char TOSTRING[300];
Boolean INDIRECT = FALSE;
Boolean OF = FALSE;
Boolean SUF = FALSE;
Boolean SOF = FALSE;


/* internal controls */
Word Switch_Register = 0;
Boolean Halted = TRUE;
long long time = 0;

/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

Address Load_ASCII_Object_File(STRING name)
{
    Address entry_point = 0;
    Word data;

    while (get_next_line() != EOF)
        {
            char *p = line;
            while (isalnum(*p)) p++;
            *p = '\0';
            while (!is_hex(*p)) p++;
            /* two values: one at line, the other at p */
            data = hex_value(p);
            if (strcasecmp(line, "EP") == 0)
                entry_point = data;
            else
                {
                    Address addr = hex_value(line);
                    Store_Memory(addr, data);
                }
        }

    return(entry_point);
}

/* ***************************************************************** */
/*                                                                   */
/* ***************************************************************** */

int get2(void)
{
    int c1 = getc(input);
    int c2 = getc(input);
    if (debug) fprintf(stderr, "read two bytes: 0x%X, 0x%X\n", c1, c2);
    if ((c1 == EOF) || (c2 == EOF))
        {
            fprintf(stderr, "Premature EOF\n");
            exit(1);
        }
    if (c1 & (~0xFF)) fprintf(stderr, "Extra high order bits for 0x%X\n", c1);
    if (c2 & (~0xFF)) fprintf(stderr, "Extra high order bits for 0x%X\n", c2);
    int n = (c1 << 8 | c2);
    return(n);
}

Address Load_Binary_Object_File(STRING name)
{
    int c1 = getc(input);
    int c2 = getc(input);
    int c3 = getc(input);
    int c4 = getc(input);
    if (debug) fprintf(stderr, "read four bytes: 0x%X, 0x%X, 0x%X, 0x%X\n", c1, c2, c3, c4);

    if ((c1 != 'O') || (c2 != 'B') || (c3 != 'J') || (c4 != 'G'))
        {
            fprintf(stdout, "First four bytes are not OBJG: ");
            fprintf(stdout, "%s", printrep(c1));
            fprintf(stdout, "%s", printrep(c2));
            fprintf(stdout, "%s", printrep(c3));
            fprintf(stdout, "%s", printrep(c4));
            fprintf(stdout, " (%02X %02X %02X %02X)\n", c1, c2, c3, c4);

            exit(1);
        }

    Address entry_point = get2();

    int n;
    while ((n = getc(input)) != EOF)
        {
            if (debug) fprintf(stderr, "Read next block of %d bytes\n", n);
            n = n - 1;
            Address addr = get2(); n -= 2;
            while (n > 0)
                {
                    Word data = get2(); n -= 2;            
                    Store_Memory(addr, data);
                    addr += 1;
                }
        }

    return(entry_point);
}

void Load_Object_File(STRING name)
{
    Address entry_point = 0;

    Clear_Memory();

    entry_point = Load_Binary_Object_File(name);
    // if (ASCII)
    //     entry_point = Load_ASCII_Object_File(name);
    // else
    //     entry_point = Load_Binary_Object_File(name);

    time = 0;
    Halted = FALSE;
    PC = entry_point & MASK_W16;
}


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

/* constructing the opcode name for an instruction */

char opcode_name[32];

void clear_opcode(void)
{
    opcode_name[0] = '\0';
}

void clear_TOSTRING(void)
{
    TOSTRING[0] = '\0';
}

void clear_INDSTRING(void)
{
    INDSTRING[0] = '\0';
}


void append_opcode(STRING name)
{
    if (opcode_name[0] != '\0')
        strncat(opcode_name, " ", sizeof(opcode_name));
    strncat(opcode_name, name, sizeof(opcode_name));
}

void append_register(int reg)
{
    if(reg == 0)
        strncat(opcode_name, "A", sizeof(opcode_name));
    else if(reg == 1)
        strncat(opcode_name, "B", sizeof(opcode_name));
    else if(reg == 2)
        strncat(opcode_name, "C", sizeof(opcode_name));
    else
        strncat(opcode_name, "D", sizeof(opcode_name));   
}

char *get_opcode(void)
{
    return(opcode_name);
}


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

int getRealNumber(int reg)
{
    int result;
    if((reg & 0x8000) != 0)
        result = reg - 65536;
    
    else
        result = reg;
  
    return result;
}

void Check_Overflow(int number)
{    
    if(number < -32768 || number > 32767)
    {
        OF = TRUE;
        L = 0x1;
    }
}


int Decode_Instruction(INST inst)
{
    return((inst >> 12) & 0xF);
}

Address Memory_Reference_Address(Address old_PC, INST inst)
{
    /* get the addr */
    Address addr = inst & MASK_OFFSET;
    /* check for Z/C = 1 -> current page */
    if ((inst & MASK_ZC_BIT) != 0)
        addr = addr | (old_PC & ~MASK_OFFSET);
    /* check for I/D = 1 -> indirect */
    if ((inst & MASK_I_BIT) != 0)
        {
            clear_INDSTRING();
            INDIRECT = TRUE;
            char hexStr[16];
            append_opcode("I");

            strncat(INDSTRING, "M[" , sizeof(INDSTRING));
            sprintf(hexStr, "0x%04X", addr);    
            strncat(INDSTRING, hexStr , sizeof(INDSTRING));
            strncat(INDSTRING, "] -> " , sizeof(INDSTRING));
            sprintf(hexStr, "0x%04X", memory[addr]);    
            strncat(INDSTRING, hexStr , sizeof(INDSTRING));
            strncat(INDSTRING, ", " , sizeof(INDSTRING));

            addr = Fetch_Memory(addr);
            time = time + 1;
        }
    return(addr);
}

void non_memory_reg(INST inst)
{
    Boolean skipStr = FALSE;
    Boolean skipStr2 = FALSE;
    Boolean skip = FALSE;
    int regBit = (inst >> 10) & 0x3;
    Word *reg;
    char nameReg[2];
    char hexStr[16];

    if(regBit == 0)
    {
        reg = &A;
        nameReg[0] = 'A';
        nameReg[1] = '\0';
    }
    else if(regBit == 1)
    { 
        reg = &B;
        nameReg[0] = 'B';
        nameReg[1] = '\0';
    }
    else if(regBit == 2)
    {
        reg = &C;
        nameReg[0] = 'C';
        nameReg[1] = '\0';
    }
    else //if(regBit == 3)
    {
        reg = &D;
        nameReg[0] = 'D';
        nameReg[1] = '\0';
    }

    /* SM */
    if(inst & MASK_SM)
    {
        append_opcode("SM");
        append_register(regBit);

        if (TOSTRING[0] != '\0')
            strncat(TOSTRING, ", ", sizeof(TOSTRING));
        strncat(TOSTRING, nameReg, sizeof(TOSTRING));
        strncat(TOSTRING, " -> ", sizeof(TOSTRING));
        sprintf(hexStr, "0x%04X", *reg);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));

        skip = skip || ((*reg & MASK_SIGN_BIT) != 0); 
    }

    /* SZ */
    if (inst & MASK_SZ)
    {
        append_opcode("SZ");
        append_register(regBit);

        if(!skip)
        {
            if (TOSTRING[0] != '\0')
                strncat(TOSTRING, ", ", sizeof(TOSTRING));
            strncat(TOSTRING, nameReg, sizeof(TOSTRING));
            strncat(TOSTRING, " -> ", sizeof(TOSTRING));
            sprintf(hexStr, "0x%04X", *reg);
            strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        }
        skip = skip || (*reg == 0);
    }

    if(skipStr && !skipStr2)
    {
        if (TOSTRING[0] != '\0')
            strncat(TOSTRING, ", ", sizeof(TOSTRING));
        strncat(TOSTRING, nameReg, sizeof(TOSTRING));
        strncat(TOSTRING, " -> ", sizeof(TOSTRING));
        sprintf(hexStr, "0x%04X", *reg);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
    }

    /* SNL */
    if (inst & MASK_SNL)
    {
        append_opcode("SNL");
        if(TOSTRING[0] != '\0')
            strncat(TOSTRING, ", ", sizeof(TOSTRING));
        strncat(TOSTRING, "L -> ", sizeof(TOSTRING));
        sprintf(hexStr, "0x%04X", L);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));

        skip = skip || (L != 0);
    }

    /* RSS */
    if (inst & MASK_RSS)
    {
        append_opcode("RSS");
        skip = !skip;
    }

    if (skip) PC = (PC + 1) & MASK_W16;
    
    /* CL */
    if (inst & MASK_CL)
    {
        append_opcode("CL");
        append_register(regBit);
        if (TOSTRING[0] != '\0')
            strncat(TOSTRING, ", ", sizeof(TOSTRING));
        strncat(TOSTRING, "0x0000 -> ", sizeof(TOSTRING));
        strncat(TOSTRING, nameReg, sizeof(TOSTRING));
        *reg = 0x0;
    }
    
    /* CLL */
    if (inst & MASK_CLL)
    {
        append_opcode("CLL");
        if (TOSTRING[0] != '\0')
            strncat(TOSTRING, ", ", sizeof(TOSTRING));
        strncat(TOSTRING, "0x0000 -> L", sizeof(TOSTRING));
        L = 0;
    }

    /* CM */
    if (inst & MASK_CM)
    {
        append_opcode("CM");
        append_register(regBit);

        if (TOSTRING[0] != '\0')
            strncat(TOSTRING, ", ", sizeof(TOSTRING));
        strncat(TOSTRING, nameReg, sizeof(TOSTRING));
        strncat(TOSTRING, " -> ", sizeof(TOSTRING));
        // hexStr[0] = '\0';
        sprintf(hexStr, "0x%04X", *reg);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        strncat(TOSTRING, ", ", sizeof(TOSTRING));

        *reg = (~(*reg));// & 0xFFFF; 
        *reg = *reg & MASK_W16;

        // hexStr[0] = '\0';
        sprintf(hexStr, "0x%04X", *reg);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        strncat(TOSTRING, " -> ", sizeof(TOSTRING));
        strncat(TOSTRING, nameReg, sizeof(TOSTRING));
    }

    if (inst & MASK_CML)
    {
        append_opcode("CML");

        if (TOSTRING[0] != '\0')
            strncat(TOSTRING, ", ", sizeof(TOSTRING));
        strncat(TOSTRING, "L -> ", sizeof(TOSTRING));
        // hexStr[0] = '\0';
        sprintf(hexStr, "0x%04X", L);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        strncat(TOSTRING, ", ", sizeof(TOSTRING));

        L = (~L) & 0x1;

        sprintf(hexStr, "0x%04X", L);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        strncat(TOSTRING, " -> L", sizeof(TOSTRING));
    }

    if (inst & MASK_DC)
    {
        append_opcode("DC");
        append_register(regBit);

        if (TOSTRING[0] != '\0')
            strncat(TOSTRING, ", ", sizeof(TOSTRING));
        strncat(TOSTRING, nameReg, sizeof(TOSTRING));
        strncat(TOSTRING, " -> ", sizeof(TOSTRING));
        // hexStr[0] = '\0';
        sprintf(hexStr, "0x%04X", *reg);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        strncat(TOSTRING, ", ", sizeof(TOSTRING));

        Check_Overflow(getRealNumber(*reg) - 1);
        *reg = (*reg - 1);// & MASK_W16;
        *reg = *reg & MASK_W16;
        if(OF)
        {
            strncat(TOSTRING, "0x0001 -> L, ", sizeof(TOSTRING));
            OF = FALSE;
        }
        // hexStr[0] = '\0';
        sprintf(hexStr, "0x%04X", *reg);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        strncat(TOSTRING, " -> ", sizeof(TOSTRING));
        strncat(TOSTRING, nameReg, sizeof(TOSTRING));
    }

    if (inst & MASK_IN)
    {
        append_opcode("IN");
        append_register(regBit);

        if (TOSTRING[0] != '\0')
            strncat(TOSTRING, ", ", sizeof(TOSTRING));
        strncat(TOSTRING, nameReg, sizeof(TOSTRING));
        strncat(TOSTRING, " -> ", sizeof(TOSTRING));
        // hexStr[0] = '\0';
        sprintf(hexStr, "0x%04X", *reg);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        strncat(TOSTRING, ", ", sizeof(TOSTRING));

        Check_Overflow(getRealNumber(*reg) + 1);
        *reg = *reg + 1;
        *reg = *reg & MASK_W16;
        if(OF)
        {
            strncat(TOSTRING, "0x0001 -> L, ", sizeof(TOSTRING));
            OF = FALSE;
        }

        // hexStr[0] = '\0';
        sprintf(hexStr, "0x%04X", *reg);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        strncat(TOSTRING, " -> ", sizeof(TOSTRING));
        strncat(TOSTRING, nameReg, sizeof(TOSTRING));        
    }

    if(skip)
    {
        strncat(TOSTRING, ", ", sizeof(TOSTRING));
        // hexStr[0] = '\0';
        sprintf(hexStr, "0x%04X", PC);
        strncat(TOSTRING, hexStr, sizeof(TOSTRING));
        strncat(TOSTRING, " -> PC", sizeof(TOSTRING));
    }


}

Word getjk(int inst3, char* aregister, Word* val)
{
    if(inst3 == 0)
    {
        strcpy(aregister, "A");
        *val = A;
        return A;
    }
    else if(inst3 == 1)
    {
        strcpy(aregister, "B");
        *val = B;
        return B;
    }
    else if(inst3 == 2)
    {
        strcpy(aregister, "C");
        *val = C;
        return C;
    }
    else if(inst3 == 3)
    {
        strcpy(aregister, "D");
        *val = D;
        return D;
    }
    else if(inst3 == 4)
    {
        strcpy(aregister, "PC");
        *val = PC;
        return PC;
    }    
    else if(inst3 == 5)
    {
        strcpy(aregister, "PSW");
        *val = PSW;
        return PSW;
    }    
    else if(inst3 == 6)
    {
        strcpy(aregister, "SP");
        *val = SP;
        return SP;
    }    
    else// if(inst3 == 7)
    {
        strcpy(aregister, "SPL");
        *val = SPL;
        return SPL;
    }
}

void RtoR(int sub_opcode, int regI, Word j, Word k, char* iregister)
{
    Word result;
    if(sub_opcode == 0)
    {
        append_opcode("MOD"); 
        if(k == 0)
        {
            result = 0;
            OF = TRUE;
            L = 1;
        }
       
        else
            result = getRealNumber(j) % getRealNumber(k);
    }
    else if(sub_opcode == 1)
    {
        append_opcode("ADD");
        Check_Overflow(getRealNumber(j) + getRealNumber(k));
        result = j + k;
        // OVERFLOW
    }
    else if(sub_opcode == 2)
    {
        append_opcode("SUB");
        Check_Overflow(getRealNumber(j) - getRealNumber(k));
        result = j - k;
        // OVERFLOW
    }
    else if(sub_opcode == 3)
    {
        append_opcode("MUL");
        Check_Overflow(getRealNumber(j) * getRealNumber(k));

        result = j * k;
        // OVERFLOW
    }
    else if(sub_opcode == 4)
    {
        append_opcode("DIV");
        if(k == 0)
        {
            result = 0;
            OF = TRUE;
            L = 1;
        }
        else
            result = getRealNumber(j) / getRealNumber(k);
    }
    else if(sub_opcode == 5)
    {
        append_opcode("AND");
        result = j & k;
    }
    else if(sub_opcode == 6)
    {
        append_opcode("OR");
        result = j | k;
    }
    else// if(sub_opcode == 7)
    {
        append_opcode("XOR");
        result = j ^ k;
    }

    switch(regI)
    {
        case 0: A = (result & MASK_W16); strcpy(iregister, "A"); break;
        case 1: B = (result & MASK_W16); strcpy(iregister, "B"); break;
        case 2: C = (result & MASK_W16); strcpy(iregister, "C"); break;
        case 3: D = (result & MASK_W16); strcpy(iregister, "D"); break;
        case 4: PC = (result & MASK_W16); strcpy(iregister, "PC"); break;
        case 5: PSW = (result & MASK_W16); strcpy(iregister, "PSW"); break;
        case 6: SP = (result & MASK_W16); strcpy(iregister, "SP"); break;
        case 7: SPL = (result & MASK_W16); strcpy(iregister, "SPL"); break;
    }
    SP = SP & MASK_W16;
    if(SP < SPL)
        SOF = TRUE;
    else
        SOF = FALSE;
}

void getRegName(int reg)
{
    if(reg == 0)
        strcpy(regName, "A");
    else if(reg == 1)
        strcpy(regName, "B");
    else if(reg == 2)
        strcpy(regName, "C");
    else if(reg == 3)
        strcpy(regName, "D");
    else if(reg == 4)
        strcpy(regName, "PC");
    else if(reg == 5)
        strcpy(regName, "PSW");
    else if(reg == 6)
        strcpy(regName, "SP");
    else if(reg == 7)
        strcpy(regName, "SPL");
}

Word getRegValue(char *aregister)
{
    if(strlen(aregister) == 1)
    {
        if(aregister[0] == 'A')
            return A;
        
        else if(aregister[0] == 'B')
            return B;
      
        else if(aregister[0] == 'C')
            return C;
        
        else //if(aregister[0] == 'D')
            return D;
        
    }
    else if(strlen(aregister) == 2)
    {
        if(aregister[0] == 'P' && aregister[1] == 'C')
            return PC;
        
        else if(aregister[0] == 'S' && aregister[1] == 'P')
            return SP;
        
    }
    else if(strlen(aregister) == 3)
    {
        if(aregister[0] == 'P' && aregister[1] == 'S' && aregister[2] == 'W')
            return PSW;
        
        else if(aregister[0] == 'S' && aregister[1] == 'P' && aregister[2] == 'L')
            return SPL;  
    }

    else
        return 0;
}


void Execute(Address old_PC, int opcode, INST inst)
{
    Address addr;
    Word value;
    Word tempReg;
    Word tempVal;
    int lastTwo;
    int reg;
    int sub_opcode;
    int device;
    int function;
    char iregister[5];
    char jregister[5];
    Word jval;
    char kregister[5];
    Word kval;

    /* zero the opcode name */
    clear_opcode(); 

    if(opcode >= 1 && opcode <= 10)
    {
        reg = (inst >> 10) & 0x3;
        if(opcode >= 1 && opcode <= 8)
        {
            addr = Memory_Reference_Address(old_PC, inst);
            value = Fetch_Memory(addr);

            if(reg == 0)
                tempReg = A;
            
            else if(reg == 1)
                tempReg = B;
            
            else if(reg == 2)
                tempReg = C;
            
            else //if(reg == 3)
                tempReg = D;
        }
    }

    switch(opcode)
        {
            case 0: 
                lastTwo = inst & 0x3;
                if(inst == 0)
                {
                    append_opcode("NOP");
                    time = time + 1;
                }
                else if(inst == 1)
                {
                    append_opcode("HLT");
                    PSW = 0;
                    time = time + 1;
                }
                else if(inst == 2)
                {
                    append_opcode("RET");
                    SP = SP + 1;
                    PC = memory[SP];
                    if(SOF && (SP >= SPL))
                        SOF = FALSE;
                    if(SUF)
                    {
                        PSW = 0;
                        time = time + 1;
                        if(verbose)  
                            fprintf(stderr, "Time %3lld: PC=0x%04X instruction = 0x%04X (RET Stack Underflow)\n", time, old_PC, inst, get_opcode()); 
                        exit(1);  
                    }
                    time = time + 2;
                    if(SP > MASK_W16)
                    {
                        SUF = TRUE;
                        SP = SP & MASK_W16;
                    }
                }
                else
                {
                    PSW = 0;
                    fprintf(stderr, "Unknown instruction: PSW -> 0x0000\n");
                    exit(1);
                }

                break;

            case 1:
                append_opcode("ADD");
                append_register(reg);                
                
                if(reg == 0)
                { 
                    Check_Overflow(getRealNumber(A) + getRealNumber(value));
                    A = A + value;
                    A = A & MASK_W16;
                }
                else if(reg == 1)
                { 
                    Check_Overflow(getRealNumber(B) + getRealNumber(value));
                    B = B + value;
                    B = B & MASK_W16;
                }
                else if(reg == 2)
                { 
                    Check_Overflow(getRealNumber(C) + getRealNumber(value));
                    C = C + value;
                    C = C & MASK_W16;
                }
                else
                { 
                    Check_Overflow(getRealNumber(D) + getRealNumber(value));
                    D = D + value; 
                    D = D & MASK_W16;
                }
                time = time + 2;
                break;

            case 2: 
                append_opcode("SUB");
                append_register(reg);

                if(reg == 0)
                { 
                    Check_Overflow(getRealNumber(A) - getRealNumber(value));
                    A = A - value;
                    A = A & MASK_W16;
                }
                else if(reg == 1)
                { 
                    Check_Overflow(getRealNumber(B) - getRealNumber(value));
                    B = B - value;
                    B = B & MASK_W16;
                }
                else if(reg == 2)
                { 
                    Check_Overflow(getRealNumber(C) - getRealNumber(value));
                    C = C - value;
                    C = C & MASK_W16;
                }
                else
                { 
                    Check_Overflow(getRealNumber(D) - getRealNumber(value));
                    D = D - value; 
                    D = D & MASK_W16;
                }

                time = time + 2;
                break;

            case 3:
                append_opcode("MUL");
                append_register(reg);
                
                if(reg == 0)
                { 
                    Check_Overflow(getRealNumber(A) * getRealNumber(value));
                    A = A * value;
                    A = A & MASK_W16;
                }
                else if(reg == 1)
                { 
                    Check_Overflow(getRealNumber(B) * getRealNumber(value));
                    B = B * value;
                    B = B & MASK_W16;
                }
                else if(reg == 2)
                { 
                    Check_Overflow(getRealNumber(C) * getRealNumber(value));
                    C = C * value;
                    C = C & MASK_W16;
                }
                else
                { 
                    Check_Overflow(getRealNumber(D) * getRealNumber(value));
                    D = D * value; 
                    D = D & MASK_W16;
                }
                time = time + 2;
                break;

            case 4:
                
                append_opcode("DIV");
                append_register(reg);

                if(reg == 0)
                {
                    if(value == 0)
                    {
                        A = 0;
                        OF = TRUE;
                        L = 1;
                    }
                    else
                        A = getRealNumber(A) / getRealNumber(value); 
                }
                else if(reg == 1)
                {
                    if(value == 0)
                    {
                        B = 0;
                        OF = TRUE;
                        L = 1;
                    }
                    else
                        B = getRealNumber(B) / getRealNumber(value);
                }
                else if(reg == 2)
                {
                    if(value == 0)
                    {
                        C = 0;
                        OF = TRUE;
                        L = 1;
                    }
                    else
                        C = getRealNumber(C) / getRealNumber(value);
                }
                else
                {
                    if(value == 0)
                    {
                        D = 0;
                        OF = TRUE;
                        L = 1;
                    }
                    else
                        D = getRealNumber(D) / getRealNumber(value);
                }
                time = time + 2;

                break;

            case 5:
                append_opcode("AND");
                append_register(reg);

                if(reg == 0)
                    A = A & value; 
                
                else if(reg == 1)
                    B = B & value; 
                
                else if(reg == 2)
                    C = C & value;
                
                else
                    D = D & value;
                
                time = time + 2;
                break;

            case 6:
                append_opcode("OR");
                append_register(reg);

                if(reg == 0)
                    A = A | value; 
                
                else if(reg == 1)
                    B = B | value; 
                
                else if(reg == 2)
                    C = C | value;
                
                else
                    D = D | value;
                 
                time = time + 2;
                break;

            case 7:
                append_opcode("XOR");
                append_register(reg);

                if(reg == 0)
                    A = A ^ value; 
                
                else if(reg == 1)
                    B = B ^ value; 
                
                else if(reg == 2)
                    C = C ^ value;
                
                else
                    D = D ^ value;
                
                time = time + 2;
                break;

            case 8:
                append_opcode("LD");
                append_register(reg);

                if(reg == 0)
                    A = value; 
                
                else if(reg == 1)
                    B = value; 
                
                else if(reg == 2)
                    C = value;
                
                else
                    D = value;
                
                time = time + 2;
                break;

            case 9:
                addr = Memory_Reference_Address(old_PC, inst);
                append_opcode("ST");
                append_register(reg);

                if(reg == 0)
                    Store_Memory(addr, A);
                
                else if(reg == 1)
                    Store_Memory(addr, B); 
                
                else if(reg == 2)
                    Store_Memory(addr, C);
                
                else
                    Store_Memory(addr, D);
                 
                
                time = time + 2;
                break;

            case 10:
                device = (inst & MASK_IOT_DEVICE) >> 3;
                function = (inst & MASK_IOT_FUNCTION);
                
                /* check for device = 3 -- Input */
                if (device == 3)
                    {
                        append_opcode("IOT 3");
                        if(reg == 0)
                            A = getc(stdin) & MASK_W16;
                        
                        else if(reg == 1)
                            B = getc(stdin) & MASK_W16;
                        
                        else if(reg == 2)
                            C = getc(stdin) & MASK_W16;
                        
                        else //if(reg == 3)
                            D = getc(stdin) & MASK_W16;    
                    }
                /* or device = 4 -- Output */
                else if (device == 4)
                    {
                        append_opcode("IOT 4");
                        if(reg == 0)
                            putc((A & 0x3FF), stdout); 
                        
                        else if(reg == 1)
                            putc((B & 0x3FF), stdout); 
                        
                        else if(reg == 2)
                            putc((C & 0x3FF), stdout); 
                        
                        else //if(reg == 3)
                            putc((D & 0x3FF), stdout); 
                    }
                else
                    {
                        append_opcode("IOT <bad-device>");
                        fprintf(stderr, "IOT function %d to unknown device %d; halting\n", function, device);
                        Halted = TRUE;
                    }
            
                time = time + 1;
                break;

            case 11:
                sub_opcode = (inst >> 10)& 0x3;

                addr = Memory_Reference_Address(old_PC, inst);
                value = Fetch_Memory(addr);
                tempVal = value;

                if(sub_opcode == 0)
                {
                    append_opcode("ISZ");
                    value = (value + 1) & MASK_W16;
                    Store_Memory(addr, value);
                    if (value == 0) PC = (PC + 1) & MASK_W16;
                    time = time + 3;
                }
                else if(sub_opcode == 1)
                {
                    append_opcode("JMP");
                    PC = addr;
                    time = time + 1;
                }
                else if(sub_opcode == 2)
                {
                    append_opcode("CALL");
                    if(SUF)
                        SUF = FALSE;
                    if(SOF)
                    {
                        PSW = 0;
                        time = time + 1;
                        if(verbose)
                        {
                            fprintf(stderr, "Stack Pointer = 0x%04X; Stack Limit = 0x%04X\n", SP, SPL); 
                            fprintf(stderr, "Time %3lld: PC=0x%04X instruction = 0x%04X (CALL Stack Overflow): ", time, old_PC, inst);
                            fprintf(stderr, "M[0x%04X] -> 0x%04X, ", addr, value);
                        
                            fprintf(stderr, "PSW -> 0x0001, 0x0000 -> PSW\n");
                        }
                        exit(1);
                    }

                    Store_Memory(SP, PC);
                    SP = SP - 1;
                    PC = addr;
                    time = time + 2;       
                    SP = SP & MASK_W16;
                    if(SP < SPL)
                        SOF = TRUE;       
                }
                else
                {
                    PSW = 0;
                    fprintf(stderr, "Unknown instruction: PSW -> 0x0000\n");
                    exit(1);
                }

                break;

            case 12:
                sub_opcode = (inst >> 10) & 0x3;
                addr = Memory_Reference_Address(old_PC, inst);
                
                if(sub_opcode == 0)
                {
                    value = Fetch_Memory(addr);
                    append_opcode("PUSH");
                    if(SUF)
                        SUF = FALSE;
                    if(SOF)
                    {
                        PSW = 0;
                        time = time + 2;
                        if(verbose)
                        {
                            fprintf(stderr, "Stack Pointer = 0x%04X; Stack Limit = 0x%04X\n", SP, SPL); 
                            fprintf(stderr, "Time %3lld: PC=0x%04X instruction = 0x%04X (PUSH Stack Overflow): ", time, old_PC, inst);
                            fprintf(stderr, "M[0x%04X] -> 0x%04X, ", addr, value);

                            fprintf(stderr, "PSW -> 0x0001, 0x0000 -> PSW\n");
                        }
                        exit(1);
                    } 

                    Store_Memory(SP, value);
                    SP = SP - 1;
                    SP = SP & MASK_W16;

                    if(SP < SPL)
                        SOF = TRUE;
                }
                else if(sub_opcode == 1)
                {
                    SP = SP + 1;
                    value = Fetch_Memory(SP);
                    append_opcode("POP");
                    if(SOF && (SP >= SPL))
                        SOF = FALSE;
                    if(SUF && verbose)
                    {
                        PSW = 0;
                        time = time + 1;
                        if(verbose)
                            fprintf(stderr, "Time %3lld: PC=0x%04X instruction = 0x%04X (POP Stack Underflow)\n", time, old_PC, inst);   
                        exit(1);
                    }
                    Store_Memory(addr, value);
                    if(SP > MASK_W16)
                    {
                        SUF = TRUE;
                        SP = SP & MASK_W16;
                    }
                }
                else
                {
                    PSW = 0;
                    fprintf(stderr, "Unknown instruction: PSW -> 0x0000\n");
                    exit(1);
                }
                
                time = time + 3;


                break;

            // NO CASE????????????????
            case 13:
                break;

            case 14:
                sub_opcode = (inst >> 9) & 0x7;
                int regI = ((inst >> 6) & 0x7);
                RtoR(sub_opcode, regI, getjk((inst >> 3) & 0x7, jregister, &jval), getjk((inst & 0x7), kregister, &kval), iregister);

                time = time + 1;
                break;

            case 15:
                clear_TOSTRING();
                non_memory_reg(inst);
                time = time + 1;
                break;
        }

    if (verbose)
    {
        fprintf(stderr, "Time %3lld: PC=0x%04X instruction = 0x%04X (%s)", time, old_PC, inst, get_opcode());
      
        if(opcode != 0 || lastTwo != 0)
            fprintf(stderr, ": ");
        
        if(INDIRECT)
        {
            fprintf(stderr, "%s", INDSTRING);
            INDIRECT = FALSE;
        }

        if(opcode == 0)
        {
            if(lastTwo ==1)
                fprintf(stderr, "PSW -> 0x0001, 0x%04X -> PSW", PSW);
            
            else if(lastTwo == 2)
                fprintf(stderr, "SP -> 0x%04X, 0x%04X -> SP, M[0x%04X] -> 0x%04X, 0x%04X -> PC", (SP-1), SP, SP, memory[SP], memory[SP]);
        }

        else if(opcode >= 1 && opcode <= 7)
        {
            getRegName(reg);
            fprintf(stderr, "%s -> 0x%04X, M[0x%04X] -> 0x%04X, ", regName, tempReg, addr, value);
            if(OF)
            {
                fprintf(stderr, "0x0001 -> L, ");
                OF = FALSE;
            }

            fprintf(stderr, "0x%04X -> %s", getRegValue(regName), regName);
        }

        else if(opcode == 8)
        {
            getRegName(reg);
            fprintf(stderr, "M[0x%04X] -> 0x%04X, 0x%04X -> %s", addr, value, value, regName);
        }

        else if(opcode == 9)
        {
            getRegName(reg);
            fprintf(stderr, "%s -> 0x%04X, 0x%04X -> M[0x%04X]", regName, getRegValue(regName), getRegValue(regName), addr);
        }

        else if(opcode == 10)
        {
            getRegName(reg);
            if(device == 3)
                fprintf(stderr, "0x%04X -> %s", getRegValue(regName), regName);
            else if(device == 4)
                fprintf(stderr, "%s -> 0x%04X", regName, getRegValue(regName));
        }

        else if(opcode == 11)
        {
            if(sub_opcode == 0) // ISZ
            {
                fprintf(stderr, "M[0x%04X] -> 0x%04X, 0x%04X -> M[0x%04X]", addr, tempVal, value, addr);
                if(value == 0)
                    fprintf(stderr, ", 0x%04X -> PC", PC);
            }
            else if(sub_opcode == 1)
                fprintf(stderr, "0x%04X -> PC", addr);
              
            else if(sub_opcode == 2)
                fprintf(stderr, "0x%04X -> M[0x%04X], 0x%04X -> SP, 0x%04X -> PC", memory[SP+1], (SP+1), SP, addr);
        }

        else if(opcode == 12)
        {
            if(sub_opcode == 0) // push
                fprintf(stderr, "M[0x%04X] -> 0x%04X, 0x%04X -> M[0x%04X], 0x%04X -> SP", addr, value, value, (SP+1), (SP));
            
            else if(sub_opcode == 1) // pop
                fprintf(stderr, "SP -> 0x%04X, 0x%04X -> SP, M[0x%04X] -> 0x%04X, 0x%04X -> M[0x%04X]", (SP-1), SP, SP, memory[SP], memory[SP], addr);
        }

        else if(opcode == 14)
        {
            fprintf(stderr, "%s -> 0x%04X, %s -> 0x%04X, ", jregister, jval, kregister, kval);
            if(OF)
            {
                fprintf(stderr, "0x0001 -> L, ");
                OF = FALSE;
            }
            fprintf(stderr, "0x%04X -> %s",  getRegValue(iregister) ,iregister);
        }
        else //if(opcode == 15)
            fprintf(stderr, "%s", TOSTRING); 
        
        fprintf(stderr, "\n");
    }
}


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

void Interpreter(STRING name)
{
    Load_Object_File(name);

    while ( (PSW & 0x0001) != 0)
        {
            Address old_PC = PC;
            INST inst = Fetch_Memory(PC);
            PC = (PC + 1) & MASK_W16;
            int opcode = Decode_Instruction(inst);
            Execute(old_PC, opcode, inst);
        }
}


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

void scanargs(STRING s)
{
    /* check each character of the option list for
       its meaning. */

    while (*++s != '\0')
        switch (*s)
            {

            case 'D': /* debug option */
                debug = TRUE;

            case 'b': /* binary object file input */
                ASCII = FALSE;
                break;

            case 'a': /* ASCII object file input */
                ASCII = TRUE;
                break;

            case 'v': /* verbose option */
                verbose = !verbose;
                break;

            case 's': /* switch register setting */
            case 'S': /* switch register setting */
                Switch_Register = hex_value(&s[1]) & MASK_W16;
                if (debug) fprintf(stderr, "Switch Register is 0x%03X\n", Switch_Register);
                break;

            default:
                fprintf (stderr,"pdp8: Bad option %c\n", *s);
                fprintf (stderr,"usage: pdp8 [-D] file\n");
                exit(1);
            }
}



/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

int main(int argc, STRING *argv)
{
    Boolean filenamenotgiven = TRUE;

    /* main driver program.  Define the input file
       from either standard input or a name on the
       command line.  Process all arguments. */

    while (argc > 1)
        {
            argc--, argv++;
            if (**argv == '-')
                scanargs(*argv);
            else
                {
                    filenamenotgiven = FALSE;
                    input = fopen(*argv,"r");
                    if (input == NULL)
                        {
                            fprintf (stderr, "Can't open %s\n",*argv);
                        }
                    else
                        {
                            Interpreter(*argv);
                            fclose(input);
                        }
                }
        }

    if (filenamenotgiven)
        {
            input = stdin;
            Interpreter(NULL);
        }

    exit(0);
}
