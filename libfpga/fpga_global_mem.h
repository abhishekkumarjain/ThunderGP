#ifndef __FPGA_GLOBAL_MEM_H__
#define __FPGA_GLOBAL_MEM_H__

#include "graph_fpga.h"

#include "fpga_application.h"


#define WRITE_BACK_BURST_LENGTH     (64)
#define LOG_WRITE_BURST_LEN         (6)


#define CU_SIZE                     (4)



template <typename T>
void burstRead(
    uint_raw                        edge_offset,
    uint_raw                        edge_end,
    uint16                          *input,
    hls::stream<T>                  &outputstream
)
{
#pragma HLS function_instantiate variable=input
#pragma HLS function_instantiate variable=outputstream
    unsigned int offset = 0;
    unsigned int end = ((edge_end - 1) >> (LOG2_SIZE_BY_INT + LOG_BURSTBUFFERSIZE)) + 1;


    burst_raw read_buffer[BURSTBUFFERSIZE];

    {

timeLineLoop : for (unsigned int i = (offset); i < (end); i ++)
        {
#if 0
            int chunk_size;
            if ((i + BURSTBUFFERSIZE * SIZE_BY_INT) > (end))
            {
                chunk_size = ((end) - i) >> LOG2_SIZE_BY_INT;
            }
            else
            {
                chunk_size = BURSTBUFFERSIZE;
            }
#else

#define  chunk_size  (BURSTBUFFERSIZE)

#endif

            for (int inner_idx = 0 ; inner_idx < chunk_size; inner_idx ++) {
#pragma HLS PIPELINE II=1
                read_buffer[inner_idx] = input[((uint_raw)(i) << LOG_BURSTBUFFERSIZE) + inner_idx];
            }

            for (int inner_idx = 0; inner_idx < chunk_size; inner_idx++)
            {
#pragma HLS PIPELINE II=1
                burst_token token;
                token.data = read_buffer[inner_idx];
                token.flag = FLAG_RESET;
                write_to_stream(outputstream, token);
            }
        }
    }

    {
        burst_token token;
        token.flag = FLAG_SET;
        write_to_stream(outputstream, token);
        return;
    }

    return;
}




void writeBack(int idx, uint16 *addr, hls::stream<burst_raw>  &input)
{
#pragma HLS function_instantiate variable=idx

    burst_raw ram[WRITE_BACK_BURST_LENGTH];

    unsigned int counter = 0;
    while (true)
    {

        for (int i = 0; i < WRITE_BACK_BURST_LENGTH; i++)
        {
#pragma HLS PIPELINE II=1
            burst_raw tmp;
            read_from_stream(input, tmp);
            if (tmp.range(BURST_ALL_BITS - 1, BURST_ALL_BITS - 32) == ENDFLAG)
            {
                return;
            }
            ram[i] = tmp;
        }
        for (int j = 0; j < WRITE_BACK_BURST_LENGTH; j++)
        {
#pragma HLS PIPELINE II=1
            addr[(counter << LOG_WRITE_BURST_LEN) + j] = ram[j];
        }
        counter ++;

    }
}


template <typename T>
void burstReadLite(
    uint_raw                        edge_offset,
    uint_raw                        edge_end,
    uint16                          *input,
    hls::stream<T>                  &outputstream
)
{
#pragma HLS function_instantiate variable=input
#pragma HLS function_instantiate variable=outputstream
    unsigned int offset = edge_offset  >> (LOG2_SIZE_BY_INT + LOG_BURSTBUFFERSIZE);
    unsigned int end = ((edge_end - 1) >> (LOG2_SIZE_BY_INT + LOG_BURSTBUFFERSIZE)) + 1;


    T read_buffer[BURSTBUFFERSIZE];

    {

timeLineLoop : for (unsigned int i = (offset); i < (offset + end); i ++)
        {
#if 0
            int chunk_size;
            if ((i + BURSTBUFFERSIZE * SIZE_BY_INT) > (end))
            {
                chunk_size = ((end) - i) >> LOG2_SIZE_BY_INT;
            }
            else
            {
                chunk_size = BURSTBUFFERSIZE;
            }
#else

#define  chunk_size_lite  (BURSTBUFFERSIZE)

#endif

            for (int inner_idx = 0 ; inner_idx < chunk_size_lite; inner_idx ++) {
#pragma HLS PIPELINE II=1
                read_buffer[inner_idx] = input[((uint_raw)(i) << LOG_BURSTBUFFERSIZE) + inner_idx];
            }

            for (int inner_idx = 0; inner_idx < chunk_size_lite; inner_idx++)
            {
#pragma HLS PIPELINE II=1
                write_to_stream(outputstream, read_buffer[inner_idx]);
            }
        }
    }
    return;
}



void writeBackLite(int totalSize, uint16 *addr, hls::stream<burst_raw>  &input)
{
#pragma HLS function_instantiate variable=addr

    burst_raw ram[WRITE_BACK_BURST_LENGTH];

    unsigned int counter = 0;
    for (int loopCount = 0; loopCount < totalSize / WRITE_BACK_BURST_LENGTH / SIZE_BY_INT; loopCount++)
    {

        for (int i = 0; i < WRITE_BACK_BURST_LENGTH; i++)
        {
#pragma HLS PIPELINE II=1
            burst_raw tmp;
            read_from_stream(input, tmp);
            ram[i] = tmp;
        }
        for (int j = 0; j < WRITE_BACK_BURST_LENGTH; j++)
        {
#pragma HLS PIPELINE II=1
            addr[(counter << LOG_WRITE_BURST_LEN) + j] = ram[j];
        }
        counter ++;

    }
}


template <typename T>
void  cuDuplicate ( int               loopNum,
                    hls::stream<T>    &input,
                    hls::stream<T>    (&output)[CU_SIZE])
{
#pragma HLS function_instantiate variable=input
    for (int i = 0; i < loopNum ; i++)
    {
#pragma HLS PIPELINE II=1
        T  unit;
        read_from_stream(input, unit);
        for (int j = 0; j < CU_SIZE; j ++)
        {
#pragma HLS UNROLL
            write_to_stream(output[j], unit);
        }
    }
}


template <typename T>
void  cuMerge ( int               loopNum,
                hls::stream<T>    (&input)[CU_SIZE],
                hls::stream<T>    &output)
{
#pragma HLS function_instantiate variable=input
    for (int i = 0; i < loopNum ; i++)
    {
#pragma HLS PIPELINE II=1
        T  unit[CU_SIZE];
#pragma HLS ARRAY_PARTITION variable=unit dim=0 complete

        for (int j = 0; j < CU_SIZE; j ++)
        {
#pragma HLS UNROLL
            read_from_stream(input[j], unit[j]);
        }
        T res;
        for (int inner = 0; inner < 16 ; inner ++)
        {
#pragma HLS UNROLL
            uint_raw tmp1 = PROP_COMPUTE_STAGE4(
                                unit[0].range(31 + inner * 32, 0 + inner * 32),
                                unit[1].range(31 + inner * 32, 0 + inner * 32)
                            );
            uint_raw tmp2 = PROP_COMPUTE_STAGE4(
                                unit[2].range(31 + inner * 32, 0 + inner * 32),
                                unit[3].range(31 + inner * 32, 0 + inner * 32)
                            );
            uint_raw rtmp = PROP_COMPUTE_STAGE4(
                                tmp1,
                                tmp2
                            );
            res.range(31 + inner * 32, 0 + inner * 32) = rtmp;
        }

        write_to_stream(output, res);
    }
}



#endif /*__FPGA_GLOBAL_MEM_H__ */
