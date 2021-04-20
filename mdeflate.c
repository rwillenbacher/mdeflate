/*
Copyright (c) 2015, Ralf Willenbacher
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in
   the documentation and/or other materials provided with the
   distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <memory.h>

#define MDEFLATE_DEBUG_PRINTF 0
#define WITH_LITERAL_ONLY_TREE 1

#define MDEFLATE_MAX_LITERAL_NODE   15
#define MDEFLATE_MAX_LITERAL_NODES  16
#define MDEFLATE_END_OF_BLOCK_NODE  16
#define MDEFLATE_LENGTH_NODES_OFFSET ( MDEFLATE_END_OF_BLOCK_NODE + 1 )
#define MDEFLATE_MAX_LENGTH_NODES    8
#define MDEFLATE_MATCH_LENGTH_OFFSET 3
#define MDEFLATE_MAX_MATCH_LENGTH  256
#define MDEFLATE_MAX_SYMBOL_NODES ( MDEFLATE_END_OF_BLOCK_NODE + MDEFLATE_MAX_LENGTH_NODES + 1 )
#define MDEFLATE_MAX_OFFSET_NODES  32
#define MDEFLATE_MAX_CODEBOOK_BACK ( 1 << 14 )

#define MDEFLATE_BLOCK_SIZE ( 1 << 14 )
#define MDEFLATE_MAX_CW_LENGTH 8

#define MDEFLATE_MAX_BL_NODES ( MDEFLATE_MAX_CW_LENGTH + 1 )
#define MDEFLATE_MAX_BL_CW_LENGTH 7


/* ------------------------ COMPRESS ------------------------ */


typedef struct {
	int32_t i_cw;
	int32_t i_cw_length;
	int32_t i_count;
	int32_t i_order;
} encnode_t;

typedef struct treenode_s {
	int32_t i_order;
	int32_t i_count;
	struct treenode_s *ps_parent;
	struct treenode_s *rgps_children[ 2 ];
} treenode_t;

typedef struct {
	int32_t i_max_codebook_back;
	int32_t i_codebook_back;
	int32_t i_max_match_length;
	encnode_t rgs_symbol_nodes[ MDEFLATE_MAX_SYMBOL_NODES ];
#if WITH_LITERAL_ONLY_TREE
	encnode_t rgs_literal_nodes[ MDEFLATE_MAX_LITERAL_NODES ];
#endif
	encnode_t rgs_offset_nodes[ MDEFLATE_MAX_OFFSET_NODES ];
	encnode_t rgs_bl_nodes[ MDEFLATE_MAX_BL_NODES ];
	int32_t i_treenode_alloc;
	treenode_t s_headnode;
	treenode_t rgs_treenodes[ 64 * 2 ];
	int32_t i_max_depth;

	int32_t i_num_overflow;
	int32_t rgi_cw_length_counts[ MDEFLATE_MAX_CW_LENGTH + 1 ];

	int32_t i_symbol_count;
	uint8_t rgui8_symbols[ MDEFLATE_BLOCK_SIZE + 1 ];
	int32_t i_length_and_offset_count;
	int32_t rgi_length_and_offset[ MDEFLATE_BLOCK_SIZE + 1 ];

	uint32_t ui_cw;
	int32_t i_cw_bits;
	int32_t i_bitstream_size;
	uint8_t *pui8_bitstream;

	uint8_t rgui8_offset_lut[ MDEFLATE_MAX_CODEBOOK_BACK ];
	uint32_t rgui_offset_offset[ MDEFLATE_MAX_CODEBOOK_BACK ];
	uint8_t rgui8_length_lut[ MDEFLATE_MAX_MATCH_LENGTH ];
	uint32_t rgui_length_offset[ MDEFLATE_MAX_MATCH_LENGTH ];
} mdeflate_compress_t;

const int32_t rgi_length_extra[ MDEFLATE_MAX_LENGTH_NODES ] = { 0, 1, 2, 3, 4, 5, 6, 7 };
const int32_t rgi_offset_extra[ MDEFLATE_MAX_OFFSET_NODES ] = { 0, 1, 2, 4, 6, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8 };


void mdeflate_write_bits( mdeflate_compress_t *ps_compress, int32_t i_cw, int32_t i_cw_length )
{
	ps_compress->ui_cw |= i_cw << ( 32 - i_cw_length - ps_compress->i_cw_bits );
	ps_compress->i_cw_bits += i_cw_length;

	while( ps_compress->i_cw_bits >= 8 )
	{
		ps_compress->pui8_bitstream[ ps_compress->i_bitstream_size++ ] = ps_compress->ui_cw >> 24;
		ps_compress->ui_cw <<= 8;
		ps_compress->i_cw_bits -= 8;
	}
}


void mdeflate_init_length_and_offset_table( mdeflate_compress_t *ps_compress )
{
	int32_t i_offset_symbol_idx, i_offset, i_idx, i_offset_size, i_length, i_length_size, i_length_symbol_idx;

	i_offset = 0;
	for( i_offset_symbol_idx = 0; i_offset_symbol_idx < MDEFLATE_MAX_OFFSET_NODES; i_offset_symbol_idx++ )
	{
		ps_compress->rgui_offset_offset[ i_offset_symbol_idx ] = i_offset;
		i_offset_size = 1 << rgi_offset_extra[ i_offset_symbol_idx ];
		for( i_idx = 0; i_idx < i_offset_size; i_idx++ )
		{
			ps_compress->rgui8_offset_lut[ i_offset++ ] = i_offset_symbol_idx;
		}
	}

	i_length = 0;
	for( i_length_symbol_idx = 0; i_length_symbol_idx < MDEFLATE_MAX_LENGTH_NODES; i_length_symbol_idx++ )
	{
		ps_compress->rgui_length_offset[ i_length_symbol_idx ] = i_length;
		i_length_size = 1 << rgi_length_extra[ i_length_symbol_idx ];
		for( i_idx = 0; i_idx < i_length_size; i_idx++ )
		{
			ps_compress->rgui8_length_lut[ i_length++ ] = i_length_symbol_idx;
		}
	}

	ps_compress->i_max_codebook_back = i_offset - 1;
	ps_compress->i_max_match_length = i_length - 1;
#if MDEFLATE_DEBUG_PRINTF > 0
	printf("max codebook back: %d, max match length: %d\n", ps_compress->i_max_codebook_back, ps_compress->i_max_match_length );
#endif
}


void mdeflate_assign_cw_r( mdeflate_compress_t *ps_compress, treenode_t *ps_treenode, int32_t i_depth )
{
	if( i_depth > ps_compress->i_max_depth )
	{
		ps_compress->i_num_overflow++;
	}

	if( ps_treenode->i_order >= 0 )
	{
		if( i_depth > ps_compress->i_max_depth )
		{
			ps_compress->rgi_cw_length_counts[ ps_compress->i_max_depth ]++;
		}
		else
		{
			ps_compress->rgi_cw_length_counts[ i_depth ]++;
		}
	}
	else
	{
		mdeflate_assign_cw_r( ps_compress, ps_treenode->rgps_children[ 0 ], i_depth + 1 );
		mdeflate_assign_cw_r( ps_compress, ps_treenode->rgps_children[ 1 ], i_depth + 1 );
	}
}

int32_t mdeflate_construct_tree( mdeflate_compress_t *ps_compress, encnode_t *ps_nodes, int32_t i_num_nodes, int32_t i_max_cw_length )
{
	int32_t i_node, i_non_zero, i_last_non_zero, i_highest, i_highest_count, i_order_zero, i_idx, i_osearch, i_order;

	i_non_zero = 0;
	for( i_node = 0; i_node < i_num_nodes; i_node++ )
	{
		ps_nodes[ i_node ].i_cw = 0;
		ps_nodes[ i_node ].i_cw_length = 0;
		ps_nodes[ i_node ].i_order = -1;
		if( ps_nodes[ i_node ].i_count > 0 )
		{
			i_non_zero++;
			i_last_non_zero = i_node;
		}
	}
	for( i_idx = 0; i_idx <= ps_compress->i_max_depth; i_idx++ )
	{
		ps_compress->rgi_cw_length_counts[ i_idx ] = 0;
	}

	memset( ps_compress->rgs_treenodes, 0, sizeof( ps_compress->rgs_treenodes ) );
	
	ps_compress->i_max_depth = i_max_cw_length;

	if( i_non_zero == 0 )
	{
		return 0; /* offset nodes without match */
	}

	if( i_non_zero == 1 ) /* force at least 2 nodes */
	{
		if( i_last_non_zero < i_num_nodes - 1 )
		{
			ps_nodes[ i_num_nodes - 1 ].i_count = 1;
		}
		else
		{
			ps_nodes[ i_num_nodes - 2 ].i_count = 1;
		}
		i_non_zero++;
	}
	
	for( i_idx = 0; i_idx < i_non_zero; i_idx++ )
	{
		i_highest = -1;
		i_highest_count = 0;
		for( i_node = 0; i_node < i_num_nodes; i_node++ )
		{
			if( ps_nodes[ i_node ].i_order < 0 && ps_nodes[ i_node ].i_count > i_highest_count )
			{
				i_highest_count = ps_nodes[ i_node ].i_count;
				i_highest = i_node;
			}
		}
		if( i_highest < 0 )
		{
			fprintf( stderr, "order sorting failure\n");
			exit( -1 );
		}
		ps_nodes[ i_highest ].i_order = i_idx;
		if( i_idx == 0 )
		{
			i_order_zero = i_highest;
		}
	}

	ps_compress->s_headnode.rgps_children[ 0 ] = &ps_compress->rgs_treenodes[ 0 ];
	ps_compress->s_headnode.rgps_children[ 1 ] = NULL;
	ps_compress->rgs_treenodes[ 0 ].i_order = 0;
	ps_compress->rgs_treenodes[ 0 ].i_count = ps_nodes[ i_order_zero ].i_count;
	ps_compress->rgs_treenodes[ 0 ].ps_parent = &ps_compress->s_headnode;
	ps_compress->i_treenode_alloc = 1;

	for( i_idx = 1; i_idx < i_non_zero; i_idx++ )
	{
		treenode_t *ps_treenode, *ps_new_branch, *ps_tree, *ps_parent;
		for( i_node = 0; i_node < i_num_nodes; i_node++ )
		{
			if( ps_nodes[ i_node ].i_order == i_idx )
			{
				break;
			}
		}

		ps_treenode = &ps_compress->rgs_treenodes[ ps_compress->i_treenode_alloc++ ];
		ps_new_branch = &ps_compress->rgs_treenodes[ ps_compress->i_treenode_alloc++ ];

		ps_treenode->i_order = i_idx;
		ps_treenode->i_count = ps_nodes[ i_node ].i_count;
		ps_new_branch->i_order = -1;
		ps_new_branch->rgps_children[ 1 ] = ps_treenode;
		ps_treenode->ps_parent = ps_new_branch;

		ps_tree = ps_compress->s_headnode.rgps_children[ 0 ];
		while( ps_tree->i_order < 0 && ps_tree->i_count > ps_treenode->i_count )
		{
			ps_tree = ps_tree->rgps_children[ 1 ];
		}
		ps_new_branch->ps_parent = ps_tree->ps_parent;
		ps_new_branch->rgps_children[ 0 ] = ps_tree;
		ps_tree->ps_parent = ps_new_branch;

		if( ps_new_branch->ps_parent->rgps_children[ 0 ] == ps_tree )
		{
			ps_new_branch->ps_parent->rgps_children[ 0 ] = ps_new_branch;
		}
		else
		{
			ps_new_branch->ps_parent->rgps_children[ 1 ] = ps_new_branch;
		}
		ps_new_branch->i_count += ps_tree->i_count;
		
		ps_tree = ps_new_branch;
		while( ps_tree->ps_parent != &ps_compress->s_headnode )
		{
			treenode_t *ps_swap;

			ps_tree->i_count += ps_treenode->i_count;

			ps_parent = ps_tree->ps_parent;
			if( ps_parent->rgps_children[ 0 ]->i_count < ps_parent->rgps_children[ 1 ]->i_count )
			{
				ps_swap = ps_parent->rgps_children[ 0 ];
				ps_parent->rgps_children[ 0 ] = ps_parent->rgps_children[ 1 ];
				ps_parent->rgps_children[ 1 ] = ps_swap;
			}
			ps_tree = ps_parent;
		}
	}

	mdeflate_assign_cw_r( ps_compress, ps_compress->s_headnode.rgps_children[ 0 ], 0 );

	if( ps_compress->i_num_overflow != 0 )
	{
		while( ps_compress->i_num_overflow )
		{
			for( i_idx = ps_compress->i_max_depth - 1; i_idx > 0; i_idx-- )
			{
				if( ps_compress->rgi_cw_length_counts[ i_idx ] > 0 )
				{
					ps_compress->rgi_cw_length_counts[ i_idx ]--;
					ps_compress->rgi_cw_length_counts[ i_idx + 1 ] += 2;
					ps_compress->rgi_cw_length_counts[ ps_compress->i_max_depth ]--;
					ps_compress->i_num_overflow -= 2;
					break;
				}
			}
		}
	}

	i_order = 0;
	for( i_idx = 1; i_idx <= ps_compress->i_max_depth; i_idx++ )
	{
		for( i_node = 0; i_node < ps_compress->rgi_cw_length_counts[ i_idx ]; i_node++ )
		{
			for( i_osearch = 0; i_osearch < i_num_nodes; i_osearch++ )
			{
				if( ps_nodes[ i_osearch ].i_order == i_order )
				{
					ps_nodes[ i_osearch ].i_cw_length = i_idx;
					i_order++;
					break;
				}
			}
		}
	}
	return i_non_zero;
}



void mdeflate_assign_cw( mdeflate_compress_t *ps_compress, encnode_t *ps_nodes, int32_t i_num_nodes, treenode_t *ps_treenode )
{
	int32_t i_cw_length, i_cw, i_node;

	i_cw = 0;
	for( i_cw_length = 1; i_cw_length <= ps_compress->i_max_depth; i_cw_length++ )
	{
		i_cw <<= 1;
		for( i_node = 0; i_node < i_num_nodes; i_node++ )
		{
			if( ps_nodes[ i_node ].i_cw_length == i_cw_length )
			{
#if MDEFLATE_DEBUG_PRINTF > 1
				int32_t i_idx;
				for( i_idx = 0; i_idx < i_cw_length; i_idx++ )
				{
					printf("%d", i_cw & ( 1 << ( i_cw_length - i_idx - 1 ) ) ? 1 : 0 );
				}
				printf(" - %d ( %d )\n", i_node, ps_nodes[ i_node ].i_count );
#endif

				ps_nodes[ i_node ].i_cw = i_cw++;
			}
		}
	}
	if( i_cw != ( ( 1 << ps_compress->i_max_depth ) ) )
	{
		fprintf( stderr, "codeword assignment failure ! ( %d %d)\n", i_cw, 1 << ps_compress->i_max_depth );
		exit( -1 );
	}
}


int32_t mdeflate_find_match( mdeflate_compress_t *ps_compress, uint8_t *pui8_search, int32_t i_offset_from_start, int32_t i_search_end, int32_t *pi_offset )
{
	int32_t i_max_match_length, i_max_back, i_offset, i_best_offset, i_best_match_length, i_match;


	i_max_match_length = i_search_end - i_offset_from_start;
	if( i_max_match_length > ps_compress->i_max_match_length )
	{
		i_max_match_length = ps_compress->i_max_match_length;
	}
	i_max_back = i_offset_from_start + ps_compress->i_codebook_back;
	if( i_max_back > ps_compress->i_max_codebook_back )
	{
		i_max_back = ps_compress->i_max_codebook_back;
	}
	i_offset = 1;
	
	i_best_offset = i_best_match_length = 0;
	*pi_offset = 0;
	
	while( i_offset < i_max_back )
	{
		i_match = 0;
		if( pui8_search[ -i_offset ] == pui8_search[ 0 ] && pui8_search[ -i_offset + i_best_match_length ] == pui8_search[ i_best_match_length ] )
		{
			i_match = 1;
			while( pui8_search[ -i_offset + i_match ] == pui8_search[ i_match ] && i_match < i_max_match_length )
			{
				i_match++;
			}	
		}
		if( i_match > i_best_match_length )
		{
			i_best_match_length = i_match;
			i_best_offset = i_offset;
		}
		i_offset++;
	}

	if( i_best_match_length == MDEFLATE_MATCH_LENGTH_OFFSET ) /* sanity */
	{
		int32_t i_offset_symbol, i_offset_size;
		i_offset_symbol = ps_compress->rgui8_offset_lut[ i_offset - 1 ];
		i_offset_size = rgi_offset_extra[ i_offset_symbol ];
		if( ( i_offset_size + 14 ) > ( i_best_match_length * 8 ) )
		{
			i_best_match_length = 0;
		}
	}

	if( i_best_match_length >= MDEFLATE_MATCH_LENGTH_OFFSET )
	{
		*pi_offset = i_best_offset;
		return i_best_match_length;
	}
	return 0;
}


int32_t mdeflate_enc_block( uint8_t *pui8_in_data, int32_t i_in_data_length, uint8_t *pui8_out_data, int32_t i_cb_back )
{
	int32_t i_idx, i_length_and_offset_idx, i_length_literal, i_length_bcopy, i_next_match_length, i_next_offset;
	
	mdeflate_compress_t s_compress;

	memset( &s_compress, 0, sizeof( s_compress ) );

	s_compress.pui8_bitstream = pui8_out_data;
	mdeflate_init_length_and_offset_table( &s_compress );
	s_compress.i_codebook_back = i_cb_back;

	i_length_literal = i_length_bcopy = 0;

	i_next_match_length = -1;
	for( i_idx = 0; i_idx < i_in_data_length; )
	{
		int32_t i_symbol, i_offset_symbol, i_length, i_offset;

		if( i_next_match_length < 0 )
		{
			i_length = mdeflate_find_match( &s_compress, &pui8_in_data[ i_idx ], i_idx, i_in_data_length, &i_offset );
			if( i_length > 0 && ( i_idx + 1 ) < i_in_data_length )
			{
				i_next_match_length = mdeflate_find_match( &s_compress, &pui8_in_data[ i_idx + 1 ], i_idx + 1, i_in_data_length, &i_next_offset );
			}
			if( i_next_match_length > i_length )
			{
				i_length = 0;
			}
			else
			{
				i_next_match_length = -1;
			}
		}
		else
		{
			i_length = i_next_match_length;
			i_offset = i_next_offset;
			i_next_match_length = -1;
		}
		if( i_length > 0 )
		{
			i_idx += i_length;
			i_length_bcopy += i_length;

#if MDEFLATE_DEBUG_PRINTF > 1
			printf("match %d %d\n", i_length, i_offset );
#endif

			i_length -= MDEFLATE_MATCH_LENGTH_OFFSET;
			i_symbol = s_compress.rgui8_length_lut[ i_length ];
			i_length -= s_compress.rgui_length_offset[ i_symbol ];
			i_symbol += MDEFLATE_LENGTH_NODES_OFFSET;

			i_offset -= 1;
			i_offset_symbol = s_compress.rgui8_offset_lut[ i_offset ];
			i_offset -= s_compress.rgui_offset_offset[ i_offset_symbol ];
			
			s_compress.rgs_symbol_nodes[ i_symbol ].i_count++;
			s_compress.rgui8_symbols[ s_compress.i_symbol_count++ ] = i_symbol;

			s_compress.rgs_offset_nodes[ i_offset_symbol ].i_count++;
			s_compress.rgui8_symbols[ s_compress.i_symbol_count++ ] = i_offset_symbol;
			s_compress.rgi_length_and_offset[ s_compress.i_length_and_offset_count++ ] = i_length;
			s_compress.rgi_length_and_offset[ s_compress.i_length_and_offset_count++ ] = i_offset;			
		}
		else
		{
			i_symbol = pui8_in_data[ i_idx ] & 0xf;
			s_compress.rgs_symbol_nodes[ i_symbol ].i_count++;
			s_compress.rgui8_symbols[ s_compress.i_symbol_count++ ] = i_symbol;

			i_symbol = ( pui8_in_data[ i_idx ] >> 4 ) & 0xf;
#if !WITH_LITERAL_ONLY_TREE
			s_compress.rgs_symbol_nodes[ i_symbol ].i_count++;
#else
			s_compress.rgs_literal_nodes[ i_symbol ].i_count++;
#endif
			s_compress.rgui8_symbols[ s_compress.i_symbol_count++ ] = i_symbol;

			i_idx += 1;
			i_length_literal += 1;
		}
	}
	s_compress.rgs_symbol_nodes[ MDEFLATE_END_OF_BLOCK_NODE ].i_count++;
	s_compress.rgui8_symbols[ s_compress.i_symbol_count++ ] = MDEFLATE_END_OF_BLOCK_NODE;

	mdeflate_construct_tree( &s_compress, &s_compress.rgs_symbol_nodes[ 0 ], MDEFLATE_MAX_SYMBOL_NODES, MDEFLATE_MAX_CW_LENGTH );
	mdeflate_assign_cw( &s_compress, &s_compress.rgs_symbol_nodes[ 0 ], MDEFLATE_MAX_SYMBOL_NODES, s_compress.s_headnode.rgps_children[ 0 ] );
#if WITH_LITERAL_ONLY_TREE
	if( mdeflate_construct_tree( &s_compress, &s_compress.rgs_literal_nodes[ 0 ], MDEFLATE_MAX_LITERAL_NODES, MDEFLATE_MAX_CW_LENGTH ) )
	{
		mdeflate_assign_cw( &s_compress, &s_compress.rgs_literal_nodes[ 0 ], MDEFLATE_MAX_LITERAL_NODES, s_compress.s_headnode.rgps_children[ 0 ] );
	}
#endif
	if( mdeflate_construct_tree( &s_compress, &s_compress.rgs_offset_nodes[ 0 ], MDEFLATE_MAX_OFFSET_NODES, MDEFLATE_MAX_CW_LENGTH ) )
	{
		mdeflate_assign_cw( &s_compress, &s_compress.rgs_offset_nodes[ 0 ], MDEFLATE_MAX_OFFSET_NODES, s_compress.s_headnode.rgps_children[ 0 ] );
	}


	for( i_idx = 0; i_idx < MDEFLATE_MAX_SYMBOL_NODES; i_idx++ )
	{
		s_compress.rgs_bl_nodes[ s_compress.rgs_symbol_nodes[ i_idx ].i_cw_length ].i_count++;
	}
#if WITH_LITERAL_ONLY_TREE
	for( i_idx = 0; i_idx < MDEFLATE_MAX_LITERAL_NODES; i_idx++ )
	{
		s_compress.rgs_bl_nodes[ s_compress.rgs_literal_nodes[ i_idx ].i_cw_length ].i_count++;
	}
#endif
	for( i_idx = 0; i_idx < MDEFLATE_MAX_OFFSET_NODES; i_idx++ )
	{
		s_compress.rgs_bl_nodes[ s_compress.rgs_offset_nodes[ i_idx ].i_cw_length ].i_count++;
	}

	mdeflate_construct_tree( &s_compress, &s_compress.rgs_bl_nodes[ 0 ], MDEFLATE_MAX_BL_NODES, MDEFLATE_MAX_BL_CW_LENGTH );
	mdeflate_assign_cw( &s_compress, &s_compress.rgs_bl_nodes[ 0 ], MDEFLATE_MAX_BL_NODES, s_compress.s_headnode.rgps_children[ 0 ] );

	
	for( i_idx = 0; i_idx < MDEFLATE_MAX_BL_NODES; i_idx++ )
	{
		mdeflate_write_bits( &s_compress, s_compress.rgs_bl_nodes[ i_idx ].i_cw_length, 3 );
	}
	

	for( i_idx = 0; i_idx < MDEFLATE_MAX_SYMBOL_NODES; i_idx++ )
	{
		int32_t i_bl_idx = s_compress.rgs_symbol_nodes[ i_idx ].i_cw_length;
		mdeflate_write_bits( &s_compress, s_compress.rgs_bl_nodes[ i_bl_idx ].i_cw, s_compress.rgs_bl_nodes[ i_bl_idx ].i_cw_length );
	}
#if WITH_LITERAL_ONLY_TREE
	for( i_idx = 0; i_idx < MDEFLATE_MAX_LITERAL_NODES; i_idx++ )
	{
		int32_t i_bl_idx = s_compress.rgs_literal_nodes[ i_idx ].i_cw_length;
		mdeflate_write_bits( &s_compress, s_compress.rgs_bl_nodes[ i_bl_idx ].i_cw, s_compress.rgs_bl_nodes[ i_bl_idx ].i_cw_length );
	}
#endif
	for( i_idx = 0; i_idx < MDEFLATE_MAX_OFFSET_NODES; i_idx++ )
	{
		int32_t i_bl_idx = s_compress.rgs_offset_nodes[ i_idx ].i_cw_length;
		mdeflate_write_bits( &s_compress, s_compress.rgs_bl_nodes[ i_bl_idx ].i_cw, s_compress.rgs_bl_nodes[ i_bl_idx ].i_cw_length );
	}

#if MDEFLATE_DEBUG_PRINTF > 0
	printf( "stats: literal: %db, bcopy: %db, tot: %d\n", i_length_literal, i_length_bcopy, i_length_literal + i_length_bcopy );
#endif

	i_length_and_offset_idx = 0;
	for( i_idx = 0; i_idx < s_compress.i_symbol_count; i_idx++ )
	{
		int32_t i_symbol, i_offset_symbol;
		i_symbol = s_compress.rgui8_symbols[ i_idx ];
#if MDEFLATE_DEBUG_PRINTF > 2
		printf("esym %d\n", i_symbol );
#endif
		mdeflate_write_bits( &s_compress, s_compress.rgs_symbol_nodes[ i_symbol ].i_cw, s_compress.rgs_symbol_nodes[ i_symbol ].i_cw_length );
		if( i_symbol > MDEFLATE_END_OF_BLOCK_NODE )
		{
			mdeflate_write_bits( &s_compress, s_compress.rgi_length_and_offset[ i_length_and_offset_idx++ ], rgi_length_extra[ i_symbol - MDEFLATE_LENGTH_NODES_OFFSET ] );
			i_offset_symbol = s_compress.rgui8_symbols[ ++i_idx ];
			mdeflate_write_bits( &s_compress, s_compress.rgs_offset_nodes[ i_offset_symbol ].i_cw, s_compress.rgs_offset_nodes[ i_offset_symbol ].i_cw_length );
			mdeflate_write_bits( &s_compress, s_compress.rgi_length_and_offset[ i_length_and_offset_idx++ ], rgi_offset_extra[ i_offset_symbol ] );
#if MDEFLATE_DEBUG_PRINTF > 2
			printf("eoff %d\n", i_offset_symbol );
#endif
		}
		else if( i_symbol <= MDEFLATE_MAX_LITERAL_NODE )
		{
			i_symbol = s_compress.rgui8_symbols[ ++i_idx ];
#if MDEFLATE_DEBUG_PRINTF > 2
		printf("esym2 %d\n", i_symbol );
#endif
			if( i_symbol > MDEFLATE_MAX_LITERAL_NODE )
			{
				fprintf( stderr, "compression error, second literal symbol is no literal symbol\n");
				exit( 1 );
			}
#if !WITH_LITERAL_ONLY_TREE
			mdeflate_write_bits( &s_compress, s_compress.rgs_symbol_nodes[ i_symbol ].i_cw, s_compress.rgs_symbol_nodes[ i_symbol ].i_cw_length );
#else
			mdeflate_write_bits( &s_compress, s_compress.rgs_literal_nodes[ i_symbol ].i_cw, s_compress.rgs_literal_nodes[ i_symbol ].i_cw_length );
#endif
		}
	}
	if( s_compress.i_cw_bits > 0 )
	{
		s_compress.pui8_bitstream[ s_compress.i_bitstream_size++ ] = s_compress.ui_cw >> 24;
	}
	return s_compress.i_bitstream_size;
}



/* ------------------------ UNCOMPRESS ------------------------ */

typedef struct {
	int8_t i8_bits;
	uint16_t ui16_cw;
	uint8_t *pui8_bitstream;

	uint8_t ui8_out_slot;
	uint8_t *pui8_out;

	uint8_t rgui8_symbol_lut[ 1 << MDEFLATE_MAX_CW_LENGTH ];
	uint8_t rgui8_symbol_length_lut[ MDEFLATE_MAX_SYMBOL_NODES ];
#if WITH_LITERAL_ONLY_TREE
	uint8_t rgui8_literal_lut[ 1 << MDEFLATE_MAX_CW_LENGTH ];
	uint8_t rgui8_literal_length_lut[ MDEFLATE_MAX_LITERAL_NODES ];
#endif
	uint8_t rgui8_offset_lut[ 1 << MDEFLATE_MAX_CW_LENGTH ];
	uint8_t rgui8_offset_length_lut[ MDEFLATE_MAX_OFFSET_NODES ];

	uint16_t rgui16_offset_offset[ MDEFLATE_MAX_OFFSET_NODES ];
	uint8_t rgui8_length_offset[ MDEFLATE_MAX_SYMBOL_NODES ];
} minflate_uncompress_t;


uint8_t minflate_read_bits( minflate_uncompress_t *ps_uncompress, uint8_t ui8_length )
{
	uint8_t ui8_cw;
	ui8_cw = ps_uncompress->ui16_cw >> ( 16 - ui8_length );
	ps_uncompress->ui16_cw <<= ui8_length;
	ps_uncompress->i8_bits -= ui8_length;
	if( ps_uncompress->i8_bits < 0 )
	{
		ps_uncompress->ui16_cw |= ( *( ps_uncompress->pui8_bitstream++ ) ) << ( -ps_uncompress->i8_bits );
		ps_uncompress->i8_bits += 8;
	}
	return ui8_cw;
}


void minflate_init_length_and_offset_table( minflate_uncompress_t *ps_uncompress )
{
	int32_t i_symbol_idx, i_symbol_offset;

	i_symbol_offset = 0;
	for( i_symbol_idx = 0; i_symbol_idx < MDEFLATE_MAX_OFFSET_NODES; i_symbol_idx++ )
	{
		ps_uncompress->rgui16_offset_offset[ i_symbol_idx ] = i_symbol_offset;
		i_symbol_offset += 1 << rgi_offset_extra[ i_symbol_idx ];
	}

	i_symbol_offset = 0;
	for( i_symbol_idx = 0; i_symbol_idx < MDEFLATE_MAX_LENGTH_NODES; i_symbol_idx++ )
	{
		ps_uncompress->rgui8_length_offset[ i_symbol_idx ] = i_symbol_offset;
		i_symbol_offset += 1 << rgi_length_extra[ i_symbol_idx ];
	}
}


void minflate_assign_cw( minflate_uncompress_t *ps_uncompress, int8_t i8_num_nodes, uint8_t *pui8_length_lut, uint8_t *pui8_lut )
{
	int8_t i8_cw_length, i8_node;
	uint8_t ui8_cw, ui8_idx;
	
	ui8_cw = 0;
	for( i8_cw_length = 1; i8_cw_length <= MDEFLATE_MAX_CW_LENGTH; i8_cw_length++ )
	{
		for( i8_node = 0; i8_node < i8_num_nodes; i8_node++ )
		{
			if( pui8_length_lut[ i8_node ] == i8_cw_length )
			{
				uint8_t ui8_dim = 1 << ( 8 - i8_cw_length );
				for( ui8_idx = 0; ui8_idx < ui8_dim; ui8_idx++ )
				{
					pui8_lut[ ui8_cw++ ] = i8_node;
				}
			}
		}
	}
}


void minflate_read_and_assign_bl_cw( minflate_uncompress_t *ps_uncompress, int8_t i8_num_nodes, uint8_t *pui8_length_lut, uint8_t *pui8_lut )
{
	int8_t i8_node;

	for( i8_node = 0; i8_node < i8_num_nodes; i8_node++ )
	{
		pui8_length_lut[ i8_node ] = minflate_read_bits( ps_uncompress, 3 );
#if MDEFLATE_DEBUG_PRINTF > 1
		printf("node %d, l %d\n", i8_node, pui8_length_lut[ i8_node ] );
#endif
	}

	minflate_assign_cw( ps_uncompress, i8_num_nodes, pui8_length_lut, pui8_lut );
}

uint8_t minflate_read_symbol( minflate_uncompress_t *ps_uncompress, uint8_t *pui8_node_lut, uint8_t *pui8_node_length_lut )
{
	uint8_t ui8_sym, ui8_len;

	ui8_sym = pui8_node_lut[ ps_uncompress->ui16_cw >> 8 ];
	ui8_len = pui8_node_length_lut[ ui8_sym ];

	ps_uncompress->ui16_cw <<= ui8_len;
	ps_uncompress->i8_bits -= ui8_len;
	if( ps_uncompress->i8_bits < 0 )
	{
		ps_uncompress->ui16_cw |= ( *( ps_uncompress->pui8_bitstream++ ) ) << ( -ps_uncompress->i8_bits );
		ps_uncompress->i8_bits += 8;
	}

	return ui8_sym;
}

void minflate_read_lengths( minflate_uncompress_t *ps_uncompress, int8_t i8_num_nodes, uint8_t *pui8_bl_length_lut, uint8_t *pui8_bl_lut, uint8_t *pui8_length_lut )
{
	int8_t i8_idx;

	for( i8_idx = 0; i8_idx < i8_num_nodes; i8_idx++ )
	{
		pui8_length_lut[ i8_idx ] = minflate_read_symbol( ps_uncompress, pui8_bl_lut, pui8_bl_length_lut );
	}
}


int32_t minflate_dec_block( uint8_t *pui8_in_data, int32_t i_in_data_length, uint8_t *pui8_out_data )
{
	minflate_uncompress_t s_uncompress;
	uint8_t ui8_sym;
	int32_t i_length_literal, i_length_bcopy;

	memset( &s_uncompress, 0, sizeof( s_uncompress ) );

	minflate_init_length_and_offset_table( &s_uncompress );

	s_uncompress.i8_bits = 8;
	s_uncompress.ui16_cw = ( pui8_in_data[ 0 ] << 8 ) | pui8_in_data[ 1 ];
	s_uncompress.pui8_bitstream = pui8_in_data + 2;

	s_uncompress.ui8_out_slot = 0;
	s_uncompress.pui8_out = pui8_out_data;

	minflate_read_and_assign_bl_cw( &s_uncompress, MDEFLATE_MAX_BL_NODES, &s_uncompress.rgui8_symbol_lut[ 0 ], &s_uncompress.rgui8_offset_lut[ 0 ] );

	minflate_read_lengths( &s_uncompress, MDEFLATE_MAX_SYMBOL_NODES, &s_uncompress.rgui8_symbol_lut[ 0 ], &s_uncompress.rgui8_offset_lut[ 0 ], &s_uncompress.rgui8_symbol_length_lut[ 0 ] );
#if WITH_LITERAL_ONLY_TREE
	minflate_read_lengths( &s_uncompress, MDEFLATE_MAX_LITERAL_NODES, &s_uncompress.rgui8_symbol_lut[ 0 ], &s_uncompress.rgui8_offset_lut[ 0 ], &s_uncompress.rgui8_literal_length_lut[ 0 ] );
#endif
	minflate_read_lengths( &s_uncompress, MDEFLATE_MAX_OFFSET_NODES, &s_uncompress.rgui8_symbol_lut[ 0 ], &s_uncompress.rgui8_offset_lut[ 0 ], &s_uncompress.rgui8_offset_length_lut[ 0 ] );

	minflate_assign_cw( &s_uncompress, MDEFLATE_MAX_SYMBOL_NODES, &s_uncompress.rgui8_symbol_length_lut[ 0 ], &s_uncompress.rgui8_symbol_lut[ 0 ] );
#if WITH_LITERAL_ONLY_TREE
	minflate_assign_cw( &s_uncompress, MDEFLATE_MAX_LITERAL_NODES, &s_uncompress.rgui8_literal_length_lut[ 0 ], &s_uncompress.rgui8_literal_lut[ 0 ] );
#endif
	minflate_assign_cw( &s_uncompress, MDEFLATE_MAX_OFFSET_NODES, &s_uncompress.rgui8_offset_length_lut[ 0 ], &s_uncompress.rgui8_offset_lut[ 0 ] );


	i_length_literal = i_length_bcopy = 0;

	do {
		ui8_sym = minflate_read_symbol( &s_uncompress, s_uncompress.rgui8_symbol_lut, s_uncompress.rgui8_symbol_length_lut );
		if( ui8_sym <= MDEFLATE_MAX_LITERAL_NODE )
		{
#if !WITH_LITERAL_ONLY_TREE
			ui8_sym |= ( minflate_read_symbol( &s_uncompress, s_uncompress.rgui8_symbol_lut, s_uncompress.rgui8_symbol_length_lut ) ) << 4;
#else
			ui8_sym |= ( minflate_read_symbol( &s_uncompress, s_uncompress.rgui8_literal_lut, s_uncompress.rgui8_literal_length_lut ) ) << 4;
#endif
			*( s_uncompress.pui8_out++ ) = ui8_sym;
			ui8_sym = 0;
			i_length_literal++;
		}
		else if( ui8_sym >= MDEFLATE_LENGTH_NODES_OFFSET )
		{
			uint8_t ui8_length_sym, ui8_offset_sym, ui8_length, *pui8_bcopy, ui8_length_extra;
			int16_t i16_offset, i16_offset_extra;

			ui8_length_sym = ui8_sym - MDEFLATE_LENGTH_NODES_OFFSET;
			ui8_length = s_uncompress.rgui8_length_offset[ ui8_length_sym ];
			ui8_length_extra = minflate_read_bits( &s_uncompress, rgi_length_extra[ ui8_length_sym ] );
			ui8_length += ui8_length_extra;
			ui8_length += MDEFLATE_MATCH_LENGTH_OFFSET;
			ui8_offset_sym = minflate_read_symbol( &s_uncompress, s_uncompress.rgui8_offset_lut, s_uncompress.rgui8_offset_length_lut );
			i16_offset = s_uncompress.rgui16_offset_offset[ ui8_offset_sym ];
			i16_offset_extra = minflate_read_bits( &s_uncompress, rgi_offset_extra[ ui8_offset_sym ] );
			i16_offset += i16_offset_extra;
			i16_offset += 1;

#if MDEFLATE_DEBUG_PRINTF > 1
			printf("bcopy %d %d (%d %d )\n", ui8_length, i16_offset, ui8_length_extra, i16_offset_extra );
#endif
			i_length_bcopy += ui8_length;

			pui8_bcopy = s_uncompress.pui8_out - i16_offset;
			while( ui8_length > 0 )
			{
				*( s_uncompress.pui8_out++ ) = *( pui8_bcopy++ );
				ui8_length--;
			}
		}
	} while( ui8_sym != MDEFLATE_END_OF_BLOCK_NODE );

#if MDEFLATE_DEBUG_PRINTF > 0
	printf("ustats: %d %d %d\n", i_length_literal, i_length_bcopy, i_length_literal + i_length_bcopy );
#endif

	return ( int32_t ) ( s_uncompress.pui8_out - pui8_out_data );
}


/* ------------------------ MAIN ------------------------ */


int main( int i_argc, char *argv[ ] )
{
	FILE *f_in, *f_out;
	uint8_t rgui8_data[ MDEFLATE_BLOCK_SIZE ];
	uint8_t rgui8_edata[ MDEFLATE_BLOCK_SIZE + MDEFLATE_BLOCK_SIZE / 5 ];
	uint8_t rgui8_ddata[ MDEFLATE_BLOCK_SIZE ];
	int32_t i_data_size, i_edata_size, i_ddata_size, i_ret, i_cb_size;

	if( i_argc < 3 )
	{
		printf("usage: <option> infile outfile\nwhere option is either 'c' for compress or 'd' for decompress\n");
		exit( 1 );
	}
	
	if( argv[ 1 ][ 0 ] == 'c' && argv[ 1 ][ 1 ] == 0 )
	{
		f_in = fopen( argv[ 2 ], "rb" );
		if( f_in == NULL )
		{
			printf("unable to open \"%s\" for reading\n", argv[ 2 ] );
			exit( 1 );
		}
		f_out = fopen( argv[ 3 ], "wb" );
		if( f_out == NULL )
		{
			printf("unable to open \"%s\" for writing\n", argv[ 3 ] );
			exit( 1 );
		}

		i_cb_size = 0;
		while( 1 )
		{
			i_ret = fread( &rgui8_data[ MDEFLATE_BLOCK_SIZE / 2 ], sizeof( uint8_t ), MDEFLATE_BLOCK_SIZE / 2, f_in ); /* / 2 because of nibbles */
#if MDEFLATE_DEBUG_PRINTF > 0
			printf("block, %d bytes\n", i_ret );
#endif
			if( i_ret > 0 )
			{
				i_data_size = i_ret;
				i_edata_size = mdeflate_enc_block( &rgui8_data[ MDEFLATE_BLOCK_SIZE / 2 ], i_data_size, &rgui8_edata[ 2 ], i_cb_size );
				rgui8_edata[ 0 ] = ( i_edata_size >> 8 ) & 0xff;
				rgui8_edata[ 1 ] = ( i_edata_size      ) & 0xff;
				i_ret = fwrite( rgui8_edata, i_edata_size + 2, sizeof( uint8_t ), f_out );
				printf( "%d %d ( %.2f )\n", i_data_size, i_edata_size, ( ( float ) i_edata_size ) / ( ( float )i_data_size ) );
				memcpy( &rgui8_data[ ( MDEFLATE_BLOCK_SIZE / 2 ) - i_data_size ], &rgui8_data[ MDEFLATE_BLOCK_SIZE / 2 ], sizeof( uint8_t ) * i_data_size );
				i_cb_size = i_data_size;
			}
			else
			{
				rgui8_edata[ 0 ] = 0;
				rgui8_edata[ 1 ] = 0;
				i_edata_size = 2;
				i_ret = fwrite( rgui8_edata, i_edata_size, sizeof( uint8_t ), f_out );
				break;
			}
		}
	}
	else if( argv[ 1 ][ 0 ] == 'd' && argv[ 1 ][ 1 ] == 0 )
	{
		uint16_t ui16_blocksize;

		f_in = fopen( argv[ 2 ], "rb" );
		if( f_in == NULL )
		{
			printf("unable to open \"%s\" for reading\n", argv[ 2 ] );
			exit( 1 );
		}
		f_out = fopen( argv[ 3 ], "wb" );
		if( f_out == NULL )
		{
			printf("unable to open \"%s\" for writing\n", argv[ 3 ] );
			exit( 1 );
		}
		
		while( 1 )
		{
			ui16_blocksize = fgetc( f_in ) << 8;
			ui16_blocksize |= fgetc( f_in );
			if( ui16_blocksize > 0 )
			{
				i_ret = fread( &rgui8_edata[ 0 ], ui16_blocksize, 1, f_in );
				i_ddata_size = minflate_dec_block( rgui8_edata, ui16_blocksize * sizeof( uint8_t ), &rgui8_ddata[ MDEFLATE_BLOCK_SIZE / 2 ] );
				printf( "%d -> %d ( %.2f )\n", ui16_blocksize + 2, i_ddata_size, ( ( float ) ui16_blocksize ) / ( ( float ) i_ddata_size ) );
				fwrite( &rgui8_ddata[ MDEFLATE_BLOCK_SIZE / 2 ], i_ddata_size  * sizeof( uint8_t ), 1, f_out );
				memcpy( &rgui8_ddata[ ( MDEFLATE_BLOCK_SIZE / 2 ) - ( int32_t )i_ddata_size ], &rgui8_ddata[ MDEFLATE_BLOCK_SIZE / 2 ], i_ddata_size * sizeof( uint8_t ) );
			}
			else
			{
				break;
			}
		}
	}
	return 0;
}





