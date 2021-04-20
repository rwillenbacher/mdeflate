# mdeflate

This is the deflate algorithm adapted to use 4 bit literals for compression.  

The compression part is somewhat unoptimized and slow.  

The decompression part is optimized for low memory usage and speed, using LUTs for symbol decompression.

# Compression

Deflate works on blocks, taking previous data as a codebook. Use mdeflate_enc_block to deflate a block of data.  
```
int32_t mdeflate_enc_block( uint8_t *pui8_in_data, int32_t i_in_data_length, uint8_t *pui8_out_data, int32_t i_codebook_back )
```
**pui8_in_data points** to the data being compressed with **i_codebook_back bytes** of data preceeding it which is available when decompressing, for example if it was decompressed by a previous block. **i_in_data_length** is the size of the data to be compressed in bytes. This size should not exceed MDEFLATE_BLOCK_SIZE / 2 bytes. The function returns the number of bytes of the compressed block which got written to **pui8_out_data**.

# Decompression

Inflate works in compressed blocks the compression function produced.

```
int32_t minflate_dec_block( uint8_t *pui8_in_data, int32_t i_in_data_length, uint8_t *pui8_out_data )
```
**pui8_in_data** points to the compressed block data of **i_in_data_length** size. The function returns the size of the decompressed data which got stored in **pui8_out_data**.


# Notes

Look at the main() function of the mdeflate.c file for a cheap compress/decompress example usage.  

Block size has some effect on compression efficiency, it might be beneficial to try multiple block sizes with lookahead and some sort of trellis for compression gain. I have not investigated this further.  

To save some bytes in the decompression structure at the cost of compression efficiency use:
```
#define WITH_LITERAL_ONLY_TREE 0
```
The LITERAL_ONLY_TREE is used to do huffman compression of the upper 4 bits of a byte while the lower 4 bit of a byte also share their huffman table with codebook lookup symbols. Setting the define to 0 will produce a single huffman table for the lower and upper 4 bits - saving a LUT in memory at the cost of compression efficiency.
