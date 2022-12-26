/* 
 * This file is part of the PCjr/utility distribution (https://github.com/guldmuddypaws/utility).
 * Copyright (c) 2022 Jason R Neuhaus
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// Compile with VS as:
// cl /EHsc crc.cpp

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <utility> // for C++ 98, include <algorithm> instead

union Register
{
 uint16_t x; // ax,bx,etc.
  struct
  {
    uint8_t l; // al,bl,etc.
    uint8_t h; // ah,bh, etc.
  };
};

static const char it_argument[]   = "/it";
static const char jrc_argument[]  = "/jrc";

static const unsigned short jrc_header_size = 512; // 0x200

int main(int argc, char* argv[])
{
 // See if the "/it" or "/jrc" option is specified.
  bool ignore_last_2_characters = false;
  bool jrc_file                 = false;

  bool invalid_argument         = false;

  const unsigned short last_argument = argc - 1;
  
  // Check arguments
  // argv[0] is this executive
  // argv[argc-1] is the filename
  for ( unsigned short i = 1 ; i < argc ; ++i )
  {
    // Make sure a valid flag is read.
    if ( strcmp(argv[i],it_argument) == 0 )
    {
      // Mark invalid if we already found this argument
      ignore_last_2_characters = true;
    }
    else if ( strcmp(argv[i],jrc_argument ) == 0 )
    {
      // Mark invalid if we already found this argument
      jrc_file = true;
    }
    // Mark as invalid if this is not the last argument or if it is the last
    // argument and does not start with a '/' (i.e. this is the filename as it
    // should be).
    else if ( ( i != last_argument ) ||
	      ( ( i == last_argument ) && argv[i][0] == '/' ) )
    {
      invalid_argument = true;
    }
  }

  // See if we have a filename properly specified. Leave at 0 pointer if not
  const char* filename = 0;

  if ( argc >= 2 )
  {
    // filename must be the last argument (argc-1)
    filename = argv[last_argument];

    // Check to see if this was really an argument
    if ( filename != 0 && filename[0] == '/' )
    {
      filename = 0;
    }
  }

  // Make sure argument are correct
  if ( ( filename == 0 ) || invalid_argument )
  {
    if ( filename == 0 )
    {
      std::cerr << "No or invalid file specified.\n";
    }
    if ( invalid_argument )
    {
      std::cerr << "Invalid argument\n";
    }

    std::cerr << "Usage:\n"
	      << argv[0] << " [/it] [/jrc] filename\n\n"
	      << "/it  = optional argument to ignore last 2 bytes of file "
                 "(ignore tail).\n"
	      << "/jrc = optional argument to ignore first 512 bytes of file "
                 "(JRC PCjr Cartridge File Format - .jrc).\n";
    return -1;
  }

  // Attempt to open the file specified
  std::ifstream input_file(filename, std::ios::in | std::ios::binary);

  if ( !input_file.good() )
  {
    std::cerr << "Error opening input file: '" << filename << "'\n";
    return -1;
  }

  // Determine the file size
  std::streampos file_size = input_file.tellg();
  input_file.seekg(0, std::ios::end);
  file_size = input_file.tellg() - file_size;
  input_file.seekg(0);

  std::cout << "Input file '" << filename << "' is " << file_size
	    << " bytes\n";

  const unsigned int number_of_bytes = file_size;
  char* cartridge_data = 0;
  try
  {
    cartridge_data = new char[number_of_bytes];
  }
  catch ( const std::bad_alloc& bad_alloc )
  {
    std::cerr << "Could not allocate memory!, error '"
	      << bad_alloc.what() << "'\n";
    return -1;
  }

  Register ax;
  Register bx;
  Register cx;
  Register dx;

  cx.x = number_of_bytes;

  // read the data from the file specified so it can be processed
  unsigned int byte_index = 0;
  char temp_char;
  while ( input_file.get(temp_char) )
  {
    assert(byte_index < number_of_bytes);

    cartridge_data[byte_index] = temp_char;
    ++byte_index;
  }
  std::cout << "Read " << byte_index << " bytes\n";

  // If we should ignore the last 2 bytes (to simplify CRC calculations
  // on raw cartridge data), loop 2 less bytes
  if ( ignore_last_2_characters )
  {
    cx.x = cx.x - 2;
  }
  if ( jrc_file )
  {
    // Header will not be processed
    cx.x = cx.x - jrc_header_size;
  }
  std::cout << "bytes to process: " << cx.x << "\n";

  bx.x = cx.x;                  // MOV BX,CX
  dx.x = 0xFFFF;                // MOV DX, 0FFFFH
  ax.h = 0;                     // XOR AH,AH

  // Approximation of SI for LODSB
  unsigned int si = ( !jrc_file ? 0 : jrc_header_size );

  for (; bx.x > 0 ; --bx.x )   // DEC BX, JNZ CRC_1
  {
    ax.l  = cartridge_data[si]; // LODSB
    ++si;                       // increment si as part of LODSB
    dx.h ^= ax.l;               // XOR DH,AL
    ax.l  = dx.h;               // MOV AL,DH
    ax.x  = ax.x << 4;          // ROL AX,CL (CL=4)
    dx.x ^= ax.x;               // XOR DX,AX
    ax.x  = ax.x << 1;          // ROL AX,1
    std::swap(dx.h,dx.l);       // XCHG DH,DL
    dx.x ^= ax.x;               // XOR DX,AX
    ax.x  = ax.x >> 4;          // ROR AX,CL (CL=4)
    ax.l &= 0xE0;               // AND AL,11100000
    dx.x ^= ax.x;               // XOR DX,AX
    ax.x  = ax.x >> 1;          // ROR AX,1
    dx.h ^= ax.l;               // XOR DH,AL
  }

  // OR DX,DX ¡V sets appropriate flags based on value of DX.

  // Output CRC to terminal
  std::cout << "CRC: " << std::setw(4) << std::setfill('0')
	    << std::hex << dx.x << '\n';

  delete[] cartridge_data;
  cartridge_data = 0;

  return 0;
}
