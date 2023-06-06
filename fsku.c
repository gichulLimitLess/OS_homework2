#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//한 block의 size는 512bytes
//64개의 block으로 이루어진 partition을 가정!
#define BLOCK_SIZE 512
#define MAX_BLOCKS 64
#define DATA_BITMAP_STARTPOINT (256 * 8)

//한 block의 형태를 정의하고자 한다 (이는 나중에 Partition에 사용될 것임)
typedef struct {
    char data[BLOCK_SIZE];
} Block;

//Data Block 0번에서 root Directory 내부의 파일들의 정보를 저장하는 역할을 할 것임
//이는 이후 배열로 선언되어 Data Block 0번에 집어넣을 것이다
typedef struct {
    unsigned char inum;
    unsigned char Name[3];
} rootDirectoryInfo;

//inode 관련한 정보를 가지고 있는 구조체
typedef struct {
    unsigned int fsize; //이 파일의 크기는 몇 바이트인가?
    unsigned int blocks; //몇 개의 블럭이 이 파일에 할당되어 있는가?
    unsigned int dptr; //Direct Pointer
    unsigned int iptr; //Indirect Pointer
} inode;

//blockArray를 동적 할당하여 partition으로 사용할 것이다
//파일 입출력을 위해 File 포인터 operation_List 정의
//파일 입출력에 사용할 line을 위한 char 배열 line 정의
Block* overall_Partition;
// inode* inode_Block1;
// inode* inode_Block2;
inode* inode_Block_all;
rootDirectoryInfo* dataInfoTable;
FILE *operation_List;
char line[1024];

//i-bmap, d-bmap에서의 비트 연산을 위한 getBit (이는 bit 정보를 set할 때 사용할 것임)
void setBit(int blockIndex, int bitIndex) {
    if (blockIndex >= 0 && blockIndex < MAX_BLOCKS) {
        overall_Partition[blockIndex].data[bitIndex / 8] |= (1 << (bitIndex % 8));
    }
}

//지금 inode에 들어가 있는 fsize 값은 iptr이 가리키고 있는 값은 전부 512bytes라고 가정하고 있다
//진짜 file size를 return 해주는 함수 (필요시 사용하면 될 듯)
int getRealFileSize(inode* inode)
{
    int directed_forDptrInList_count = 0;
    int real_fsize = 0;
    //iptr을 사용하고 있지 않으면, 그대로 돌려주면 된다
    if(inode->iptr == 100)
    {
        return inode->fsize;
    }
    //iptr을 사용 중이라면
    else
    {
        directed_forDptrInList_count = (inode->blocks)-2; //dptr이 가리키는 datablock과 dptr들을 저장하는 dataBlock들은 제한다
        printf("directed 개수 : %d\n",directed_forDptrInList_count);
        real_fsize = 512 + 4*directed_forDptrInList_count + 512*(directed_forDptrInList_count-1) + ((inode->fsize) % 512);  //dptr이 가리키는 꽉 찬 data block + data block 내부에 저장되어 있는 dptr 개수 + 꽉 찬 data block 개수들 + 꽉 안찬 마지막 data block 1개
        return real_fsize;
    }
}

//특정 bit를 0으로 바꿔주는 clearBit
void clearBit(int blockIndex, int bitIndex) {
    if (blockIndex >= 0 && blockIndex < MAX_BLOCKS) {
        overall_Partition[blockIndex].data[bitIndex / 8] &= ~(1 << (bitIndex % 8));
    }
}

//i-bmap, d-bmap에서의 비트 연산을 위한 getBit (이는 bit 정보를 get할 때 사용할 것임)
int getBit(int blockIndex, int bitIndex) {
    if (blockIndex >= 0 && blockIndex < MAX_BLOCKS) {
        return (overall_Partition[blockIndex].data[bitIndex / 8] >> (bitIndex % 8)) & 1;
    }
    return -1; // Indicate an invalid bit index
}


//Data를 집어넣기 위한 함수 선언
void insertData(int blockIndex, void* data, size_t dataSize)
{
    //blockIndex가 유효한 값일 경우 (0~63인 경우)
    if(blockIndex >= 0 && blockIndex < MAX_BLOCKS)
    {
        memcpy(overall_Partition[blockIndex].data, data, dataSize);
    }
}

//inode Block들을 처음 상태로 초기화하는 것임
void initialize_inodeBlock()
{
    for(int i = 0; i<64; i++)
    {
        inode_Block_all[i].fsize = 0;
        inode_Block_all[i].blocks = 0;
        inode_Block_all[i].dptr = 100;
        inode_Block_all[i].iptr = 100; //iptr은 유효하지 않은 값인 100으로 초기화할 것이다
    }
}

//RootDirectory_DataBlock에 정보들을 집어넣을 것이다
void set_RootDirectoryDataBlock(int dataBlock_number, inode* inode_Block)
{
    //data Table 초기화 (empty 상태로)
    dataInfoTable = malloc(sizeof(rootDirectoryInfo) * 61);

    //Root Directory에 대한 initialize 해야 함 (Root Directory에 대한 inode number는 2)
    //iptr은 사용하지 않을 것임 (유효하지 않은 값인 100을 일단 집어넣어 놨음)
    inode_Block[2].fsize = 4 * 61;
    inode_Block[2].blocks = 1;
    inode_Block[2].dptr = 0;
    inode_Block[2].iptr = 100;

    //빈 테이블 initialize
    for(int i = 0; i < 61; i++)
    {
        dataInfoTable[i].inum = 0;
        dataInfoTable[i].Name[0] = ' ';
        dataInfoTable[i].Name[1] = ' ';
        dataInfoTable[i].Name[2] = '\0';
    }
    setBit(1,2); //root Directory 사용한다고 표시
    memcpy(overall_Partition[dataBlock_number+4].data, dataInfoTable, sizeof(rootDirectoryInfo) * 61);
    free(dataInfoTable);
    setBit(1, DATA_BITMAP_STARTPOINT); //root Directory에 관한 data block 사용한다고 표시

}

//Partition을 초기화하는 함수
void initialize_Partition(Block* overall_Partition)
{
    //우선 Partition 전체를 0으로 쭉 채우자
    //이 작업으로 인해, Superblock은 따로 채워줄 필요는 없어진다
    for(int i = 0; i<MAX_BLOCKS; i++)
    {
        memset(overall_Partition[i].data, 0, BLOCK_SIZE);
    }

    //i-bmap, d-bmap은 512바이트 block 한 개에 다 들어간다
    //0~255까지는 i-bmap, 256~511까지는 d-bmap, 256kb씩 차지하고 있음
    setBit(1,0);
    setBit(1,1);
    
    //inode block을 초기화할 예정이다
    initialize_inodeBlock();
    memcpy(overall_Partition[2].data, inode_Block_all, sizeof(inode) * 64);

    set_RootDirectoryDataBlock(0, ((inode*)overall_Partition[2].data));
}

//Read 연산
int read_operation(char* file_Name, int bytes)
{
    int file_inum = 0;
    int read_bytes = bytes;
    inode file_inode;

    //rootDirectory에서 해당 파일을 찾고 있으면 break
    for(int i = 0; i<61; i++)
    {
        //해당하는 이름을 가진 파일이 존재하면
        if(strcmp(((rootDirectoryInfo*)overall_Partition[4].data)[i].Name, file_Name) == 0)
        {
            file_inum = ((rootDirectoryInfo*)overall_Partition[4].data)[i].inum;
            file_inode = ((inode*)overall_Partition[2].data)[file_inum];
            break;
        }
        if(i == 60) //i가 60이 되는 동안 못 찾았으면 파일이 없는 것이므로.. -1 return
        {
            return -1; //비정상 종료임을 명시
        }
    }

    if(file_inode.fsize <= bytes) //읽어낼 바이트가 fsize보다 큰 경우 (다 읽어낼 수 있는 경우)
    {
        if(file_inode.iptr == 100) //iptr까지 안 읽어도 되는 경우
        {
            printf("%s\n",overall_Partition[file_inode.dptr+4].data);
            return 0;
        }
        else //iptr에서 추가적으로 읽어내야 하는 경우
        {
            for(int i = 0; i<512; i++)
            {
                printf("%c",overall_Partition[file_inode.dptr+4].data[i]);
            }
            
            int i = 0;
            while(((int*)overall_Partition[file_inode.iptr].data)[i] != 100) //iptr이 가리키는 곳에서의 dptr을 차근차근 읽는다
            {
                if(((int*)overall_Partition[file_inode.iptr].data)[i+1] == 100) //iptr이 가리키는 data block에서의 마지막 dptr인 경우
                {
                    printf("%s",overall_Partition[((int*)overall_Partition[file_inode.iptr].data)[i]].data);
                    break;
                }
                else
                {
                    for(int j = 0; j<512; j++)
                    {
                        printf("%c",overall_Partition[file_inode.dptr+4].data[j]);
                    }
                    i++;
                }
            }
            return 0;
            
        }
    }
    else if(file_inode.fsize > bytes) //다 못 읽어내는 경우
    {
        printf("fsize : %d\n",file_inode.fsize);
        if(bytes < 512) //iptr까지 안 읽어도 되는 경우
        {
            for(int i = 0; i<bytes; i++) //들어온 bytes 만큼만 읽는다
            {
                printf("%c",overall_Partition[file_inode.dptr+4].data[i]);
            }
            printf("\n\n");
            return 0;
        }
        else //iptr에서 추가적으로 읽어내야 하는 경우
        {
            for(int i = 0; i<512; i++)
            {
                printf("%c",overall_Partition[file_inode.dptr+4].data[i]);
            }
            
            int i = 0;
            while(((int*)overall_Partition[file_inode.iptr].data)[i] != 100) //iptr이 가리키는 곳에서의 dptr을 차근차근 읽는다
            {
                if(((int*)overall_Partition[file_inode.iptr].data)[i+1] == 100 && read_bytes < 512) //iptr이 가리키는 data block에서의 마지막 dptr인 경우
                {
                    for(int j = 0; j<read_bytes; j++)
                    {
                        printf("%c",overall_Partition[file_inode.dptr+4].data[j]);
                    }
                    break;
                }
                else //아닌 경우
                {
                    for(int j = 0; j<512; j++)
                    {
                        printf("%c",overall_Partition[file_inode.dptr+4].data[j]);
                        read_bytes--;
                    }
                    i++;
                }
            }
            printf("\n\n");
            return 0;
        }
    }
}

//Write 연산
int write_operation(char* file_Name, int bytes)
{
    unsigned int now_inum_cond = 0;
    char* exist_fileName;
    inode* file_inode;

    //이미 존재하는 file인지 아닌지 찾는 과정 (못찾으면 빠져 나올 것이다)
    for(int i = 0; i < 61; i++)
    {
        //Root Directory 정보가 들어있는 Data Block에 접근하여 거기의 inum과 Name 데이터를 비교해가며 Searching한다
        now_inum_cond = ((rootDirectoryInfo*)overall_Partition[4].data)[i].inum;
        exist_fileName = ((rootDirectoryInfo*)overall_Partition[4].data)[i].Name;
        

        //이미 존재하는 file인 경우
        if(strcmp(file_Name, exist_fileName) == 0)
        {
            file_inode = &((inode*)overall_Partition[2].data)[now_inum_cond];

            //현재 가지고 있는 Data Block들로는 write를 다 수행할 수 없을 때 (+1은 null 문자 때문에)
            if(512-((file_inode->fsize)%512) < (bytes+1))
            {
                file_inode->fsize--; //'\0' 문자 제거하고 시작할 것이므로 미리 fsize를 줄여주고 시작한다.
                
                //iptr이 유효하지 않은 경우 (dptr만 쓰고 있던 경우) (이 경우엔, iptr이 가리키는 data block 하나 추가 정의로 인하여 저장할 data block이 1개 더 필요하다)
                //dptr이 128개 들어가는 iptr이 가리키는 data block에는 직접적으로 글을 쓸 수 없다
                if(file_inode->iptr == 100)
                {
                    int write_bytes = bytes+1;
                    int usable_dataBlockCount = 0; //가용 가능한 data block의 개수를 뒤져서, 들어온 bytes만큼을 넣을 수 있는지부터 판단해야 한다
                    //1. 사용중이긴 하지만, 아직 512bytes를 다 사용하지 않은 data block부터 채운다
                    //2. data block을 꽉 채웠으면, 다른 data block을 찾아서 거기에 이어 써야 한다

                    for(int j = 0; j<60; j++) //d-bmap을 뒤진다
                    {
                        //사용하지 않는 data block을 발견한 경우
                        if(getBit(1,DATA_BITMAP_STARTPOINT+j) == 0)
                        {
                            usable_dataBlockCount++; 
                        }
                    }

                    //현재 비어있는 공간을 계산한 것 (앞으로 정의할 iptr이 가리키는 data block 1개만큼의 크기를 빼고 계산한다)
                    if(((usable_dataBlockCount-1) * 512) + (file_inode->fsize % 512) < bytes+1) 
                    {
                        return -1; //비정상 종료되었다고 하는 flag int값 넘겨주기
                    }

                    int start_point = file_inode->fsize;

                    //남은 공간에 우선 하나씩 뒤에다가 붙여쓴다
                    for(int k = start_point; k < 512; k++)
                    {
                        overall_Partition[(file_inode->dptr)+4].data[k] = file_Name[0];
                        (file_inode->fsize)++; //inode에 file size 값을 증가시킨다
                        write_bytes--; //써야 할 bytes 수를 줄인다 (그만큼 썼으니깐..)
                    }

                    int count = 0;
                    //file_iptr을 새로 가리켜야 한다
                    for(int j = 0; j<60; j++) //d-bmap을 뒤진다
                    {
                        //사용하지 않는 data block을 처음 발견한 경우 (iptr이 유효하지 않은 값(100)으로 정의되어 있는 경우)
                        if(getBit(1,DATA_BITMAP_STARTPOINT+j) == 0 && file_inode->iptr == 100)
                        {
                            int* dptr_List = malloc(sizeof(int) * 128);
                            for(int k = 0; k<128; k++)
                            {
                                dptr_List[k] = 100; //유효하지 않은 값으로 우선 쭉 집어넣는다
                            } 
                            file_inode->iptr = j; //해당 data block을 iptr가 가리키도록 하고, 거기엔 dptr_List를 집어넣을 것이다
                            memcpy(overall_Partition[j+4].data, dptr_List, sizeof(int)*128); //초기화된 dptr_List(dptr 모음집)을 해당 data block에 집어넣는다
                            file_inode->fsize = file_inode->fsize + 512; //data block 하나를 또 가지게 되었으니, 512바이트 만큼 가지게 되었다고 fsize로 표시해준다
                            file_inode->blocks++; //사용하는 block 개수를 추가해준다
                            setBit(1,DATA_BITMAP_STARTPOINT+j); //d-bmap에 사용 중이라고 바꿔준다
                            free(dptr_List);
                            continue; //for문 끝까지 가버린다
                        }

                        //이제 발견하는 data block들은 dptr로 정의하고 거기에다가 이어 써야 할 때
                        if(getBit(1,DATA_BITMAP_STARTPOINT+j) == 0 && file_inode->iptr != 100)
                        {
                            setBit(1,DATA_BITMAP_STARTPOINT+j); //d-bmap에 사용 중이라고 바꿔준다
                            ((int*)overall_Partition[(file_inode->iptr)+4].data)[count] = j;
                            if(write_bytes > 512) //1개의 data block으로 부족한 경우 (추가적으로 dptr을 선언해줘야 할 경우)
                            {
                                //남은 공간에 우선 하나씩 뒤에다가 붙여쓴다
                                for(int k = 0; k < 512; k++)
                                {
                                    overall_Partition[j+4].data[k] = file_Name[0];
                                    file_inode->fsize++; //inode에 file size 값을 증가시킨다
                                    write_bytes--; //써야 할 bytes 수를 줄인다 (그만큼 썼으니깐..)
                                }
                                count++; //dptr_List의 다음 칸에다가 dptr 이어서 적어야 하므로, count변수 증가시킨다
                                file_inode->blocks++; //사용하는 block 개수를 추가해준다
                                continue;
                            }
                            else //1개의 data block으로 이제는 충분한 경우 (마지막 경우)
                            {
                                //남은 공간에 우선 하나씩 뒤에다가 붙여쓴다
                                for(int k = 0; k < write_bytes-1; k++)
                                {
                                    overall_Partition[j+4].data[k] = file_Name[0];
                                    file_inode->fsize++; //inode에 file size 값을 증가시킨다
                                }
                                overall_Partition[j+4].data[write_bytes-1] = '\0'; //마지막엔 null문자 삽입
                                file_inode->blocks++; //사용하는 block 개수를 추가해준다
                                return 0; //정상적으로 수행되었다고 하는 flag int값 넘겨주기
                            }
                        }
                    }
                }
                else //iptr이 유효한 경우 (이 경우엔, 가지고 있는 fsize로만 계산해서 판단하면 될 것이다)
                {
                    int write_bytes = bytes+1;
                    int usable_dataBlockCount = 0; //가용 가능한 data block의 개수를 뒤져서, 들어온 bytes만큼을 넣을 수 있는지부터 판단해야 한다
                    //1. 사용중이긴 하지만, 아직 512bytes를 다 사용하지 않은 data block부터 채운다
                    //2. data block을 꽉 채웠으면, 다른 data block을 찾아서 거기에 이어 써야 한다

                    for(int j = 0; j<60; j++) //d-bmap을 뒤진다
                    {
                        //사용하지 않는 data block을 발견한 경우
                        if(getBit(1,DATA_BITMAP_STARTPOINT+j) == 0)
                        {
                            usable_dataBlockCount++; 
                        }
                    }

                    int now_empty_bytes = ((usable_dataBlockCount) * 512) + (file_inode->fsize % 512); //현재 비어있는 공간을 계산한 것
                    if(now_empty_bytes < bytes+1) //현재 비어있는 bytes가 쓰려고 하는 bytes보다 작은 경우 (쓸 수 없는 경우)
                    {
                        return -1; //비정상 종료되었다고 하는 flag int값 넘겨주기
                    }

                    int count = 0;

                    printf("\n남은 데이터 블럭 개수 값 : %d\n",usable_dataBlockCount);

                    //아직 다 안쓴 공간부터 채워줘야 하므로, iptr이 가리키는 data block에서의 dptr을 뒤지면서 찾아봐야 한다
                    for(int j = 0; j < 128; j++)
                    {
                        int dptr_inDataBlock = ((int*)overall_Partition[file_inode->iptr+4].data)[j];
                        if(dptr_inDataBlock != 100 && ((int*)overall_Partition[file_inode->iptr+4].data)[j+1] == 100) //새로 할당하기 전 마지막 dptr인 경우
                        {
                            //남은 공간에 우선 하나씩 뒤에다가 붙여쓴다
                            for(int k = 0; k < 512; k++)
                            {
                                if(overall_Partition[dptr_inDataBlock+4].data[k] == '\0') //빈 곳을 발견하면
                                {
                                    overall_Partition[dptr_inDataBlock+4].data[k] = file_Name[0]; //이어쓴다
                                    file_inode->fsize++; //inode에 file size 값을 증가시킨다
                                    write_bytes--; //써야 할 bytes 수를 줄인다 (그만큼 썼으니깐..)
                                }                         
                            }
                            count = j+1; //나중에 dptr 읽어내기를 인덱스 j+1부터 시작할 것이다
                            break;
                        }
                    }

                    //새로운 block을 무조건 찾아줘야 한다 (위의 "512-(file_fsize%512) < bytes" 조건을 만족시키지 못했기 때문이다)
                    for(int j = 0; j<60; j++) //d-bmap을 뒤진다
                    {
                        //새로 발견하는 data block들은 dptr로 정의하고 거기에다가 이어 써야 할 때
                        if(getBit(1,DATA_BITMAP_STARTPOINT+j) == 0)
                        {
                            ((int*)overall_Partition[file_inode->iptr+4].data)[count] = j;
                            if(write_bytes > 512) //1개의 data block으로 부족한 경우 (추가적으로 dptr을 선언해줘야 할 경우)
                            {
                                setBit(1,DATA_BITMAP_STARTPOINT+j); //d-bmap에 사용 중이라고 바꿔준다
                                //남은 공간에 우선 하나씩 뒤에다가 붙여쓴다
                                for(int k = 0; k < 512; k++)
                                {
                                    overall_Partition[j+4].data[k] = file_Name[0];
                                    file_inode->fsize++; //inode에 file size 값을 증가시킨다
                                    write_bytes--; //써야 할 bytes 수를 줄인다 (그만큼 썼으니깐..)
                                }
                                count++; //dptr_List의 다음 칸에다가 dptr 이어서 적어야 하므로, count변수 증가시킨다
                                ((inode*)overall_Partition[2].data)[now_inum_cond].blocks++; //사용하는 block 개수를 추가해준다
                                continue;
                            }
                            else //1개의 data block으로 충분한 경우
                            {
                                setBit(1,DATA_BITMAP_STARTPOINT+j); //d-bmap에 해당 block을 사용 중이라고 바꿔준다
                                for(int k = 0; k < write_bytes; k++)
                                {
                                    overall_Partition[j+4].data[k] = file_Name[0];
                                    file_inode->fsize++; //inode에 file size 값을 증가시킨다
                                }
                                overall_Partition[j+4].data[write_bytes] = '\0';
                                file_inode->blocks++; //사용하는 block 개수를 추가해준다

                                return 0; //정상적으로 수행되었다고 하는 flag int값 넘겨주기
                            }
                        }
                    }    
                }
            }       
            else //현재 있는 block으로도 충분히 다 적어낼 수 있는 경우
            {
                file_inode->fsize--; //'\0' 문자 제거하고 시작할 것이므로 미리 fsize를 줄여주고 시작한다.
                if(file_inode->iptr == 100) //iptr을 사용하는 경우까지 안 가도 되는 경우
                {
                    //direct pointer가 가리키는 위치에 있는 문자열 뒤에다가 하나씩 붙여쓴다
                    for(int j = 0; j < bytes+1; j++)
                    {
                        overall_Partition[file_inode->dptr+4].data[j] = file_Name[0];
                        ((inode*)overall_Partition[2].data)[now_inum_cond].fsize++; //fsize 업데이트
                    }
                    overall_Partition[file_inode->dptr+4].data[bytes+1] = '\0';
                    return 0; //제대로 수행되었다고 하는 flag int값 넘겨주기
                }
                else //iptr을 사용하는 경우까지 간 경우
                {
                    int now_dptr = -1;
                    for(int j = 0; j < 128; j++)
                    {
                        now_dptr = ((int*)overall_Partition[(file_inode->iptr)+4].data)[j];
                        //direct pointer가 적어낼 수 있는 block을 찾은 경우 (data block 내의 dptr이 유효한 값이고, 맨 마지막 공간이 비어있는 경우)
                        //나머지 경우는 위에서 다 걸러졌으니 따로 생각할 필요 없음 (data block의 마지막 부분만 0으로 비어있으면 그만임)
                        if(now_dptr != 100 && ((int*)overall_Partition[(file_inode->iptr)+4].data)[j+1] == 100)
                        {
                            //direct pointer가 가리키는 위치에 있는 문자열 뒤에다가 하나씩 붙여쓴다
                            for(int k = 0; k < bytes+1; k++)
                            {
                                overall_Partition[now_dptr+4].data[j] = file_Name[0];
                                ((inode*)overall_Partition[2].data)[now_inum_cond].fsize++; //fsize 업데이트
                            }
                            overall_Partition[now_dptr+4].data[bytes+1] = '\0';
                            return 0; //제대로 수행되었다고 하는 flag int값 넘겨주기
                        }
                    }
                }
            }
        }
    }
    //for문 내부에서 존재하는 file을 못 찾은 경우에는, 바깥으로 빠져나올 것이다
    //파일을 새로 생성해야 하는 경우이며, 파일 새로 생성하는 과정을 수행해주면 된다

    if(bytes+1 <= 512) //1. bytes가 512보다 작거나 같을 경우엔, iptr를 위한 새로운 block 할당이 필요 없다 (block 1개만 할당해주면 된다)
    {
        for(int j = 0; j<64; j++) //i-bmap을 뒤지면서 우선 inode부터 초기화해야 한다
        {
            //사용하지 않는 inode block을 발견한 경우
            if(getBit(1,j) == 0)
            {
                setBit(1,j); //해당 구역을 사용한다고 표시해둔다
                ((inode*)overall_Partition[2].data)[j].blocks = 1; //사용해야 할 block 개수 업데이트 (여기선 1개만 쓰니까 +1)
                for(int l = 0; l<61; l++) //Root Directory에 내용 추가
                {
                    if(((rootDirectoryInfo*)overall_Partition[4].data)[l].inum == 0) //root Directory에서 비어있는 칸을 찾았으면
                    {
                        ((rootDirectoryInfo*)overall_Partition[4].data)[l].inum = j; //inum 집어넣고
                        now_inum_cond = j; //다른 데서도 쓸 수 있게 변수 초기화 하고
                        //file Name 집어넣고
                        ((rootDirectoryInfo*)overall_Partition[4].data)[l].Name[0] = file_Name[0];
                        ((rootDirectoryInfo*)overall_Partition[4].data)[l].Name[1] = file_Name[1];
                        ((rootDirectoryInfo*)overall_Partition[4].data)[l].Name[2] = file_Name[2];

                        break; //하나 초기화 했으니 더 이상 필요 없음, 이제 나가라. 
                    }
                }
                break; //하나 초기화 했으니 더 이상 필요 없음, 이제 나가라.
            }
            if(j==63) //여기까지 왔는데 못 찾았으면 공간이 없는 거다
                return -1;
        }

        for(int j = 0; j<60; j++) //d-bmap을 뒤진다
        {
            //사용하지 않는 data block을 발견한 경우
            if(getBit(1,DATA_BITMAP_STARTPOINT+j) == 0)
            {
                setBit(1,DATA_BITMAP_STARTPOINT+j); //해당 비트를 사용 중이라고 바꾸고
                ((inode*)overall_Partition[2].data)[now_inum_cond].dptr = j; //direct pointer로 새로운 data block 하나 가리키도록 함
                //direct pointer가 가리키는 위치에 있는 문자열 뒤에다가 하나씩 붙여쓴다
                for(int l = 0; l < bytes+1; l++)
                {
                    overall_Partition[j+4].data[l] = file_Name[0];
                    ((inode*)overall_Partition[2].data)[now_inum_cond].fsize++; //해당 file에 대한 inode에서 fsize 업데이트
                }
                overall_Partition[j+4].data[bytes+1] = '\0'; //마지막 블럭은 null 문자를 집어넣어 준다
                ((inode*)overall_Partition[2].data)[now_inum_cond].fsize++;
                return 0; //제대로 수행되었다고 하는 flag int값 넘겨주기
            }
        }
        //사용 가능한 data block이 없으면 여기로 나올 것이다
        return -1; //비정상 종료되었다고 하는 flag int값 넘겨주기
    } 
    else if(bytes+1 > 512) //2. bytes가 512보다 클 경우엔, iptr을 위한 새로운 block 할당이 필요하다 (즉, data block 할당이 추가적으로 +1 필요하다)
    {
        int needed_BlockCount = 0;
        int remain_BlockCount = 0;
        int write_bytes = bytes+1;

        if(write_bytes % 512 > 0) //추가로 적어야 하는 블럭이 또 필요한 경우
            needed_BlockCount = (int)(write_bytes/512) + 2;
        else
            needed_BlockCount = (int)(write_bytes/512) + 1;

        for(int j = 0; j<60; j++) //d-bmap을 뒤진다
        {
            //사용하지 않는 data block을 발견한 경우
            if(getBit(1,DATA_BITMAP_STARTPOINT+j) == 0)
            {
                remain_BlockCount++; //남아있는 block Count++
            }
        }

        printf("남아있는 블럭 개수 : %d개\n",remain_BlockCount);
        printf("필요한 블럭 개수 : %d개\n",needed_BlockCount);
        
        if(needed_BlockCount > remain_BlockCount) //남아있는 data block이 부족한 경우
        {
            return -1; //비정상 종료되었다고 하는 flag int값 넘겨주기
        }
        
        for(int j = 0; j<64; j++) //i-bmap을 뒤지면서 우선 inode부터 초기화해야 한다
        {
            //사용하지 않는 inode block을 발견한 경우
            if(getBit(1,j) == 0)
            {
                setBit(1,j); //해당 구역을 사용한다고 표시해둔다
                ((inode*)overall_Partition[2].data)[j].blocks = needed_BlockCount; //사용해야 할 block 개수 업데이트
                for(int l = 0; l<61; l++) //Root Directory에 내용 추가
                {
                    if(((rootDirectoryInfo*)overall_Partition[4].data)[l].inum == 0) //root Directory에서 비어있는 칸을 찾았으면
                    {
                        ((rootDirectoryInfo*)overall_Partition[4].data)[l].inum = j; //inum 집어넣고
                        now_inum_cond = j; //다른 데서도 쓸 수 있게 변수 초기화 하고
                        //file Name 집어넣고
                        ((rootDirectoryInfo*)overall_Partition[4].data)[l].Name[0] = file_Name[0];
                        ((rootDirectoryInfo*)overall_Partition[4].data)[l].Name[1] = file_Name[1];
                        ((rootDirectoryInfo*)overall_Partition[4].data)[l].Name[2] = file_Name[2];

                        break; //하나 초기화 했으니 더 이상 필요 없음, 이제 나가라. 
                    }
                }
                break; //하나 초기화 했으니 더 이상 필요 없음, 이제 나가라.
                
            }
        }

        int count = 0; //count에 따라서 초기화 하는 data block의 형태가 달라질 것이다
        int* dptr_List = malloc(sizeof(int) * 128); //iptr이 가리키는 data block에 dptr_List를 집어넣을 것이다
        for(int i = 0; i<128; i++) //초기화
        {
            dptr_List[i] = 100;
        }

        for(int j = 0; j<60; j++) //d-bmap을 뒤지면서 가용 가능한 data block을 모두 할당한다
        {
            if(getBit(1, DATA_BITMAP_STARTPOINT+j) == 0 && count == 0) //dptr부터 초기화
            {
                setBit(1,DATA_BITMAP_STARTPOINT+j); //해당 data block 사용한다고 표시
                ((inode*)overall_Partition[2].data)[now_inum_cond].dptr = j;
                //direct pointer가 가리키는 위치에 있는 문자열 뒤에다가 하나씩 붙여쓴다
                for(int l = 0; l < 512; l++)
                {
                    overall_Partition[j+4].data[l] = file_Name[0];
                    write_bytes--;
                    ((inode*)overall_Partition[2].data)[now_inum_cond].fsize++; //해당 file에 대한 inode에서 fsize 업데이트
                }
                count++;
                continue;
            }
            if(getBit(1, DATA_BITMAP_STARTPOINT+j) == 0 && count == 1) //iptr을 초기화해야 할 때
            {
                setBit(1, DATA_BITMAP_STARTPOINT+j); //해당 data block 사용한다고 표시
                ((inode*)overall_Partition[2].data)[now_inum_cond].iptr = j;

                for(int k = 0; k<128; k++)
                {
                    dptr_List[k] = 100; //유효하지 않은 값으로 우선 쭉 집어넣는다
                } 
                memcpy(overall_Partition[j+4].data, dptr_List, sizeof(int)*128); //초기화된 dptr_List(dptr 모음집)을 해당 data block에 집어넣는다
                ((inode*)overall_Partition[2].data)[now_inum_cond].fsize += 512; //data block 하나를 또 가지게 되었으니, 512바이트 만큼 가지게 되었다고 fsize로 표시해준다
                setBit(1,DATA_BITMAP_STARTPOINT+j); //d-bmap에 사용 중이라고 바꿔준다

                free(dptr_List); //dptr_List는 더 이상 필요 없으니 free!
                count++; //다음 단계로 넘어갈 수 있도록 count++
                continue;
            }
            if(getBit(1, DATA_BITMAP_STARTPOINT+j) == 0 && count >= 2) //iptr 내부에 dptr을 넣어야 할 때
            {
                
                while(1) //dptr로 직접 가리킨 거, dptr 저장한 block 2개 빼고 만큼 계산하면 된다
                {
                    setBit(1, DATA_BITMAP_STARTPOINT+j);
                    int file_iptr = ((inode*)overall_Partition[2].data)[now_inum_cond].iptr;
                    ((int*)overall_Partition[file_iptr+4].data)[count-2] = j; //dptr 값들을 iptr이 가리키는 data block에 다 집어넣을 것이다

                    //iptr가 가리키는 data block 내부의 dptr들이 가리키는 data block들에 내용들을 집어 넣는다
                    if(write_bytes > 512) //이번에 집어넣는 data block으로 안 끝나는 경우
                    {
                        for(int l = 0; l < 512; l++)
                        {
                            overall_Partition[j+4].data[l] = file_Name[0];
                            ((inode*)overall_Partition[2].data)[now_inum_cond].fsize++; //해당 file에 대한 inode에서 fsize 업데이트
                            write_bytes--;
                        }
                        count++; //count 변수 증가 (인덱스 역할도 할 것임)
                        break; //while문을 빠져나간다 (continue문과 만나서 바깥 for문을 돌도록 한다)
                    }
                    else if(write_bytes > 0 && write_bytes <= 512) //이번이 마지막 data block 할당일 경우
                    {
                        for(int l = 0; l < write_bytes-1; l++)
                        {
                            overall_Partition[j+4].data[l] = file_Name[0];
                            ((inode*)overall_Partition[2].data)[now_inum_cond].fsize++; //해당 file에 대한 inode에서 fsize 업데이트
                        }
                        overall_Partition[j+4].data[write_bytes-1] = '\0'; //마지막 블럭은 null 문자를 집어넣어 준다
                        ((inode*)overall_Partition[2].data)[now_inum_cond].fsize++; //null을 하나 추가 했으므로
                        count = 0;
                        printf("새로 만든 %s 파일 크기 : %d\n",file_Name, ((inode*)overall_Partition[2].data)[now_inum_cond].fsize);
                        printf("새로 만든 %s 실제 파일 크기 : %d\n",file_Name, getRealFileSize(&((inode*)overall_Partition[2].data)[now_inum_cond]));
                        return 0; //제대로 수행되었다고 하는 flag int값 넘겨주기
                    }
                }
                continue; //for문을 continue 하면서 j 값을 증가시킨다
            }
        }
    }
}

//Delete 연산
int delete_operation(char* file_Name)
{
    int file_inum = 0;
    int delete_bytes = 0;
    inode* file_inode;

    //rootDirectory에서 해당 파일을 찾으면 break 후 지우는 연산 수행
    for(int i = 0; i<61; i++)
    {
        //해당하는 이름을 가진 파일이 존재하면
        if(strcmp(((rootDirectoryInfo*)overall_Partition[4].data)[i].Name, file_Name) == 0)
        {
            //inode의 주솟값과 inode number를 가져온다
            file_inum = ((rootDirectoryInfo*)overall_Partition[4].data)[i].inum;
            file_inode = &((inode*)overall_Partition[2].data)[file_inum];
            break;
        }
        if(i == 60) //i가 60이 되는 동안 못 찾았으면 파일이 없는 것이므로.. -1 return
        {
            return -1; //비정상 종료임을 명시
        }
    }

    printf("잘 동작? %d\n",getRealFileSize(file_inode));

    //삭제 작업을 실시한다
    if(file_inode->blocks >= 2) //iptr에 연결되어 있는 dptr까지 다 처리해야 하는 경우
    {
        delete_bytes = (file_inode->fsize) - 1024; //iptr이 가리키고 있는 data block 크기를 제외한 곳만 지워야 하니까
        int dptr_inList = 0;
        //iptr 관련 친구들부터 처리할 것이다
        for(int i =0; i<61; i++)
        {
            dptr_inList = ((int*)overall_Partition[(file_inode->iptr)+4].data)[i];
            
            //dptr_List의 마지막 줄에서는
            if(dptr_inList != 100 && ((int*)overall_Partition[(file_inode->iptr)+4].data)[i+1] == 100)
            {
                for(int j = 0; j<delete_bytes; j++)
                {
                    overall_Partition[dptr_inList+4].data[j] = 0; //다 0으로 그냥 초기화한다
                }
                clearBit(1,DATA_BITMAP_STARTPOINT+dptr_inList);
                break;
            }
            else if(dptr_inList != 100)
            {
                for(int j = 0; j<512; j++)
                {
                    overall_Partition[dptr_inList+4].data[j] = 0; //다 0으로 그냥 초기화한다
                    delete_bytes--;
                }
                clearBit(1,DATA_BITMAP_STARTPOINT+dptr_inList);
            }
        }
        //iptr 정보 초기화
        clearBit(1,DATA_BITMAP_STARTPOINT+(file_inode->iptr));
        file_inode->iptr = 100;
    
        //dptr에 있는 정보들도 지우고 깔끔히 다 마무리하며 끝낼 것이다
        for(int i = 0; i<512; i++)
        {
            overall_Partition[(file_inode->dptr) + 4].data[i] = 0; //다 0으로 그냥 초기화한다 
        }
        clearBit(1,DATA_BITMAP_STARTPOINT+(file_inode->dptr));
        for(int i = 0; i<60; i++)
        {
            printf("%u",getBit(1,DATA_BITMAP_STARTPOINT+i));
        }
        printf("\n");

        file_inode->blocks = 0;
        file_inode->fsize = 0;
        clearBit(1,file_inum);
        printf("%s Delete 완료\n", file_Name);
        return 0;
        
    }
    else //dptr로도 다 처리할 수 있는 경우라면
    {
        for(int i = 0; i<file_inode->fsize; i++)
        {
            overall_Partition[(file_inode->dptr) + 4].data[i] = 0; //다 0으로 그냥 초기화한다 
        }
        clearBit(1,DATA_BITMAP_STARTPOINT+(file_inode->dptr)); //dptr 정보 초기화
        file_inode->blocks = 0;
        file_inode->fsize = 0;
        clearBit(1,file_inum);
        printf("%s Delete 완료\n",file_Name);
        return 0;
    }
}

//16진수로 결과를 출력해주는 함수
void printBlockArrayTo_HexaDecimal(Block* blockArray, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < 512; j++) {
            unsigned char byteValue = (unsigned char)blockArray[i].data[j];
            for (int k = 7; k >= 0; k -= 4) {
                unsigned char hexValue = (byteValue >> k) & 0x0F;
                printf("%X", hexValue);
            }
        }
    }
}

//input으로 input_file의 이름이 들어올 것이다
//그리고 그것을 읽어서 I/O operation을 처리할 것이다
int main(int argc, char* argv[])
{  
    overall_Partition = (Block*)malloc(MAX_BLOCKS * sizeof(Block));
    inode_Block_all = malloc(sizeof(inode) * 64);

    if(overall_Partition == NULL)
     {
        printf("Memory 할당 실패했습니다!\n");
        return 1;
    }

    //Partition 초기화 작업
    initialize_Partition(overall_Partition);

    //<input_file>을 받아서 open한다
    operation_List = fopen(argv[1], "r");
    if(operation_List == NULL)
    {
        printf("파일 열기 실패\n");
        return 1;
    }

    //파일을 읽어낸다
    while(fgets(line, sizeof(line), operation_List) != NULL)
    {
        char* file_name;
        char* operation;
        char* token;
        int bytes;
        int flag;
        file_name = strtok(line, " \n"); //문자열을 split하는데, 개행문자도 포함시켜서 쪼갬으로서 공백 문제를 해결한다
        operation = strtok(NULL, " \n");    

        //Delete 연산이 아닌 경우에만 bytes 수를 읽어내면 된다!
        if(operation[0] != 'd')
        {
            bytes = atoi(strtok(NULL, " \n"));
        }
        
        if(operation[0] == 'd') //Delete 연산일 경우
        {
            flag = delete_operation(file_name);
            if(flag == -1) //비정상 종료인 경우
            {
                printf("No such file!\n");
            }
        }
        else if(operation[0] == 'w') //Write 연산일 경우
        {
            flag = write_operation(file_name, bytes);
            if(flag == -1) //비정상 종료인 경우
            {
                printf("No space!\n");
            }
            
        }
        else if(operation[0] == 'r') //Read 연산일 경우
        {
            flag = read_operation(file_name, bytes);
            if(flag == -1) //비정상 종료인 경우
            {
                printf("No such file!\n");
            }
        }
    }
    //overall_Partition의 결과를 16진수로 출력한다
    printBlockArrayTo_HexaDecimal(overall_Partition, MAX_BLOCKS);

    //malloc 했던 것들 모두 free
    free(inode_Block_all);
    free(overall_Partition);

    //열었던 file descriptor는 닫아주는 것이 원칙
    fclose(operation_List);
    return 0;
}   