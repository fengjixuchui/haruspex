/* 
	This file is not complete due to some of the dependencies I had that I cannot share, 
	but should serve as a pretty "accurate" pseudocode for the implementation.
*/
#include <array>
#include <vector>
#include <string>
#include <unordered_set>
#include <initializer_list>

// Test pad implementation.
//
#define PADDING_OPCODE  0x90
#define FAULTING_OPCODE 0xCE

static uint8_t testpad_temp[ 256 ];
[[gnu::noinline, gnu::naked, no_split, code_alignment(64)]] static uint32_t test_pad()
{
	#define _PAD_1 __emit PADDING_OPCODE } __asm {
	#define _PAD_4  _PAD_1 _PAD_1 _PAD_1 _PAD_1
	__asm 
	{
		_PAD_4 _PAD_4	          // 43 bytes of padding. (7+14+43=64)
		_PAD_4 _PAD_4	          //
		_PAD_4 _PAD_4	          //
		_PAD_4 _PAD_4	          //
		_PAD_4 _PAD_4	          //
		_PAD_1			          //
		_PAD_1			          //
		_PAD_1			          //

		// Trash the next cacheline.
		//
		lea  rax, [rip+next_line] // 14 bytes
		push qword ptr [rax]	  // 
		pop  qword ptr [rax]	  //
		sfence                    //

		// Read the counter.
		//
		xor  ecx, ecx	          // 7 bytes
		lfence			          // 
		rdpmc			          //
		next_line:
		
		// Save low part of the counter.
		//
		mov  r9d, eax
		
		// Enter the shadow of the call.
		//
		call x
			__emit      PADDING_OPCODE  // 15 byte padding for the instruction.
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			__emit      PADDING_OPCODE  //
			
			__emit      PADDING_OPCODE  // 3 byte tail.
			__emit      PADDING_OPCODE	//
			__emit      PADDING_OPCODE	//
			
			__emit      FAULTING_OPCODE // Terminator.
		x:
		
			// Waste some cycles.
			//
			vmovups    ymm0,    [testpad_temp]
			vmovups    ymm1,    [testpad_temp]
			vmovups    ymm2,    [testpad_temp]
			vmovups    ymm3,    [testpad_temp]
			vzeroupper
			addps      xmm0,    xmm1
			vaddps     ymm2,    ymm0,    ymm3
			vaddps     ymm1,    ymm0,    ymm2
			vaddps     ymm3,    ymm0,    ymm1
			vaddps     ymm0,    ymm0,    [testpad_temp]
			vaddps     ymm1,    ymm0,    [testpad_temp]
			vaddps     ymm2,    ymm0,    [testpad_temp]
			vaddps     ymm3,    ymm0,    [testpad_temp]
			vaddps     ymm0,    ymm0,    ymm1
			vaddps     ymm2,    ymm0,    ymm3
			vaddps     ymm1,    ymm0,    ymm2
			vaddps     ymm3,    ymm0,    ymm1
			vaddps     ymm0,    ymm0,    [testpad_temp]
			vaddps     ymm1,    ymm0,    [testpad_temp]
			vaddps     ymm2,    ymm0,    [testpad_temp]
			vaddps     ymm3,    ymm0,    [testpad_temp]
			vaddps     ymm0,    ymm0,    ymm1
			vaddps     ymm2,    ymm0,    ymm3
			vaddps     ymm1,    ymm0,    ymm2
			vaddps     ymm3,    ymm0,    ymm1
			vaddps     ymm0,    ymm0,    [testpad_temp]
			vaddps     ymm1,    ymm0,    [testpad_temp]
			vaddps     ymm2,    ymm0,    [testpad_temp]
			vaddps     ymm3,    ymm0,    [testpad_temp]
			vaddps     ymm0,    ymm0,    ymm1
			vaddps     ymm2,    ymm0,    ymm3
			vaddps     ymm1,    ymm0,    ymm2
			vaddps     ymm3,    ymm0,    ymm1
			lea        rax,     [rip+z]
			xchg       [rsp],   rax
			ret
		z:

		// Serialize and read counter again; return the difference.
		//
		lfence
		rdpmc
		sub  eax,    r9d
		ret
	}
	#undef _PAD_1
	#undef _PAD_4
}

// Test pad details.
//
static constexpr size_t test_pad_padding_size = 15;
static constexpr size_t test_pad_tail_size = 3;
static constexpr size_t test_pad_pfx_size = 64;
static const auto test_pad_location = [ ] ()
{
	auto r = test_pad_pfx_size + ( uint8_t* ) &test_pad;
	constexpr auto not_pad = [ ] ( auto b ) { return b != PADDING_OPCODE; };
	while ( std::find_if( r, r + test_pad_padding_size, not_pad ) != ( r + test_pad_padding_size ) )
		r++;
	return r;
}( );

// Runs the test given the instruction, the tail and the result set.
//
static void run_test( xstd::range<const uint8_t*> instruction,
					  xstd::range<const uint8_t*> tail,
					  xstd::range<uint32_t*> results,
					  const ia32::perfmon::event_selector& evt )
{
	// Write the instruction and pad it with nops.
	//
	auto* it = test_pad_location;
	it = std::copy( instruction.begin(), instruction.end(), it );
	std::fill( it, test_pad_location + test_pad_padding_size, PADDING_OPCODE );

	// Write the trailing code.
	//
	it = test_pad_location + test_pad_padding_size;
	it = std::copy( tail.begin(), tail.end(), it );
	*it = FAULTING_OPCODE;

	// Setup the processor state.
	//
	ia32::disable();
	ia32::perfmon::dynamic_set_state(
		0,
		evt,
		ia32::perfmon::ctr_enable | ia32::perfmon::ctr_supervisor,
		true
	);

	// Run the experiment.
	//
	for ( uint32_t& res : results )
	{
		// Flush the iCache for the testpad.
		//
		volatile uint8_t* ip = ( xstd::any_ptr ) &test_pad;
		std::copy_n( ip, 512, ip );
		ia32::sfence();
		for ( int64_t n = 512 - 64; n >= 0; n -= 64 )
			ia32::clflush( ip + n );

		// Repeat until we execute the test with no SMIs delivered.
		//
		while ( true )
		{
			// Reset the counter as a naive protection from overflow.
			//
			ia32::perfmon::dynamic_set_value( 0, 0 );

			// Read the SMI counter.
			//
			auto smi_ctr = ia32::read_msr( IA32_MSR_SMI_COUNT );

			// Run through the testpad.
			//
			uint32_t lres = test_pad();

			// If the SMI counter did not change, save the result and break.
			//
			if ( ia32::read_msr( IA32_MSR_SMI_COUNT ) == smi_ctr )
			{
				res = lres;
				break;
			}
		}
	}

	// Revert the changes and return.
	//
	ia32::enable();
}

// Test helpers.
//
static double test_out_of_order( xstd::range<const uint8_t*> op )
{
	uint32_t results[ 8 ];
	run_test( op, std::initializer_list<uint8_t>{ 0x0F, 0x5E, 0xE5 }, results, { .event_select = 0x14, .unit_mask = 1 } /*divisor cycles*/ );
	return xstd::percentile( xstd::sorted_clone( results ), 0.75f );
}
static double test_decode( xstd::range<const uint8_t*> op, uint8_t unit_mask )
{
	ia32::perfmon::event_selector evt = { .event_select = 0x79, .unit_mask = unit_mask };  /*decoder details*/

	// If DSB, we're just testing for validity, don't waste too much time.
	//
	if ( path == decode_path::dsb )
	{
		uint32_t results[ 1 ];
		run_test( op, std::initializer_list<uint8_t>{}, results, evt );
		return results[ 0 ];
	}

	uint32_t results[ 64 ];
	run_test( op, std::initializer_list<uint8_t>{}, results, evt );
	return xstd::mode( xstd::sorted_clone( results ) );
}
static std::array<double, 3> test_decode( xstd::range<const uint8_t*> op )
{
	return {
		test_decode( op, 0x4 ), // MITE
		test_decode( op, 0x8 ), // DSB
		test_decode( op, 0x30 ) // MS
	};
}

// Write the basic data collection logic.
//
struct result_entry
{
	std::vector<uint8_t>            opcode = {};
	uint32_t                        length = 0;

	std::array<double, 3>           uops = { 0 };
	double                          oo_cycles = 0;

	std::string                     decoding = {};
	bool                            cpl0 = true;
	bool                            valid = false;
	bool                            compat_mode = false;
	std::string_view                iclass = {};
	std::string_view                category = {};
	std::string_view                extension = {};

	// Conversion to json.
	//
	std::string to_json() const
	{
		std::string res = {};

		// Add the opcode.
		//
		res += "{ \"opcode\": [";
		for ( size_t n = 0; n != opcode.size(); n++ )
		{
			if ( ( n + 1 ) == opcode.size() )
				res += std::to_string( opcode[ n ] ) + "],";
			else
				res += std::to_string( opcode[ n ] ) + ",";
		}

		// Add the decoding.
		//
		res += "\"decoding\": \"" + decoding + "\",";

		// Add the results and decoding details, finish the entry.
		//
		res += "\"uops\": [";
		for ( size_t n = 0; n != uops.size(); n++ )
		{
			if ( ( n + 1 ) == uops.size() )
				res += std::to_string( uops[ n ] ) + "],";
			else
				res += std::to_string( uops[ n ] ) + ",";
		}

		res += "\"outOfOrder\": " + std::to_string( oo_cycles ) + ",";
		res += "\"compatMode\": " + ( compat_mode ? "true"s : "false"s ) + ",";
		res += "\"valid\": " + ( valid ? "true"s : "false"s ) + ",";
		res += "\"decLength\": " + std::to_string( length ) + ",";

		res += "\"iclass\": \""s + std::string{ iclass } + "\",";
		res += "\"category\": \""s + std::string{ category } + "\",";
		res += "\"extension\": \""s + std::string{ extension } + "\",";
		res += "\"cpl\": " + ( cpl0 ? "0"s : "3"s ) + " }";
		return res;
	}
};

[[entry_point, init_discardable]] uint64_t entry_point()
{
	// Calculate and verify the basline.
	//
	auto baseline = test_decode( std::initializer_list<uint8_t>{ 0x90 } );
	auto baseline_ft = test_decode( std::initializer_list<uint8_t>{ 0xCE } );
	xstd::log( "Baseline          : %s\n", baseline );
	xstd::log( "Faulting Baseline : %s\n", baseline_ft );
	if ( baseline_ft[ 1 ] != 0 || baseline[ 1 ] != 0 || ( baseline[ 0 ] - baseline_ft[ 0 ] ) != 15 )
	{
		xstd::log( "Aborting, invalid.\n" );
		return 0;
	}
	
	// Declare the logic for singular testing instructions, and then enter the test loop.
	//
	std::vector<result_entry> results;
	auto test_instruction = [ & ] ( std::initializer_list<uint8_t> opc )
	{
		result_entry result = { .opcode = { opc } };
	
		// Pad the opcode.
		//
		std::array<uint8_t, 15> ins;
		std::copy_n( opc.begin(), opc.size(), ins.begin() );
		std::fill_n( ins.begin() + opc.size(), ins.size() - opc.size(), PADDING_OPCODE );

		// Try decoding.
		//
		auto dec = xed::decode64( ins );
		if ( !dec )
		{
			dec = xed::decode32( ins );
			if ( dec )
				result.compat_mode = true;
		}
		if ( dec )
		{
			result.length = dec->length();
			result.iclass = xed_iclass_enum_t2str( dec->get_class() );
			result.extension = xed_extension_enum_t2str( xed_decoded_inst_get_extension( &*dec ) );
			result.category = xed_category_enum_t2str( xed_decoded_inst_get_category( &*dec ) );
			result.valid = xed_decoded_inst_valid_for_chip( &*dec, XED_CHIP_SKYLAKE );
			result.cpl0 = xed_decoded_inst_get_attribute( &*dec, XED_ATTRIBUTE_RING0 );
			result.decoding = dec->to_string();
			result.opcode.resize( std::min( result.opcode.size(), dec->length() ) );
		}

		// Try making the CPU decode, skip on failure.
		//
		result.uops = test_decode( opc );
		if ( result.uops[ 0 ] <= baseline_ft[ 0 ] && result.uops[ 2 ] <= baseline_ft[ 2 ] )
			return;

		// Do the out-of-order execution measurement and append to the list.
		//
		result.oo_cycles = test_out_of_order( opc );
		results.emplace_back( result );
	};

	static constexpr uint8_t full_prefix_list[] = {
		0x2e, 0x36, 0x3e, 0x26,
		0x40, 0x41, 0x42, 0x43,
		0x44, 0x45, 0x46, 0x47,
		0x48, 0x49, 0x4A, 0x4B,
		0x4C, 0x4D, 0x4E, 0x4F,
		0x64, 0x65, 0x66, 0x67,
		0x9b, 0xf0, 0xf2, 0xf3,
	};

	// Prefixes that in some cases change the instruction semantics.
	//
	for ( uint8_t pfx : { 0x0, 0x66, 0x9B, 0xF2, 0xF3 } )
	{
		// 2-byte opcode prefix.
		//
		for ( uint8_t pfx2 : { 0x0, 0xF } )
		{
			// Every possible opcode.
			//
			for ( size_t op = 0; op <= 0xFF; ++op )
			{
				// Skip prefixes.
				//
				if ( xstd::contains( full_prefix_list, op ) )
					continue;

				// Extension byte in case it is relevant.
				//
				for ( size_t mreg = 0; mreg <= 0xFF; ++mreg )
				{
					uint8_t suf = uint8_t( 0x90 ^ mreg );

					if ( pfx && pfx2 )
						test_instruction( { pfx, pfx2, uint8_t( op ), suf } );
					else if ( pfx )
						test_instruction( { pfx, uint8_t( op ), suf } );
					else if ( pfx2 )
						test_instruction( { pfx2, uint8_t( op ), suf } );
					else
						test_instruction( { uint8_t( op ), suf } );
				}
			}
		}
	}
	xstd::log( "Experiment complete!\n" );

	// Dump the results into JSON format.
	//
	std::string json_result = xstd::fmt::str(
		"{ "
			"\"nopBaseline\": { \"mits\": %f, \"ms\": %f }, "
			"\"faultBaseline\": { \"mits\": %f, \"ms\": %f },  "
			"\"data\": [",
		baseline[ 0 ], baseline[ 2 ],
		baseline_ft[ 0 ], baseline_ft[ 2 ] 
	);
	for ( auto& result : results )
		json_result += result.to_json() + ",";
	json_result.pop_back();
	json_result += "] }";

	/* write the JSON? */
	return 0;
}