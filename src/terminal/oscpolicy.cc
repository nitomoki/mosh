/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "src/terminal/oscpolicy.h"

using namespace Terminal;

static bool all_digits( const std::string& s )
{
  return !s.empty() && std::all_of( s.begin(), s.end(), []( char c ) { return c >= '0' && c <= '9'; } );
}

static std::wstring widen( const std::string& s )
{
  return std::wstring( s.begin(), s.end() );
}

OSCPolicy OSCPolicy::parse( const std::string& text, std::vector<std::string>& errors )
{
  OSCPolicy policy;
  std::istringstream lines( text );
  std::string line;
  int line_number = 0;

  while ( std::getline( lines, line ) ) {
    line_number++;
    std::istringstream fields( line );
    std::vector<std::string> tokens;
    std::string token;
    while ( fields >> token ) {
      tokens.push_back( token );
    }
    if ( tokens.empty() || tokens[0][0] == '#' ) {
      continue;
    }

    const std::string where = "line " + std::to_string( line_number ) + ": ";
    if ( tokens.size() < 3 || tokens.size() > 4 ) {
      errors.push_back( where + "expected NUMBER PREFIX MODE [MAXLEN]" );
      continue;
    }

    const std::string& number = tokens[0];
    if ( !all_digits( number ) || number.size() > 5 || ( number.size() > 1 && number[0] == '0' ) ) {
      errors.push_back( where + "invalid OSC number \"" + number + "\"" );
      continue;
    }
    const int value = atoi( number.c_str() );
    if ( value == 0 || value == 1 || value == 2 || value == 8 ) {
      errors.push_back( where + "OSC " + number + " is handled natively and cannot be configured" );
      continue;
    }
    if ( value == META_NUMBER ) {
      errors.push_back( where + "OSC " + number + " is reserved" );
      continue;
    }

    Rule rule;
    rule.number = widen( number );
    if ( tokens[1] != "*" ) {
      rule.prefix = widen( tokens[1] );
    }

    const std::string& mode = tokens[2];
    if ( mode == "drop" ) {
      rule.mode = Mode::Drop;
    } else if ( mode == "latest" ) {
      rule.mode = Mode::Latest;
    } else if ( mode == "always" ) {
      rule.mode = Mode::Always;
    } else {
      errors.push_back( where + "invalid mode \"" + mode + "\" (expected drop, latest or always)" );
      continue;
    }

    rule.max_length = DEFAULT_MAX_LENGTH;
    if ( tokens.size() == 4 ) {
      const std::string& max_length = tokens[3];
      if ( !all_digits( max_length ) || max_length.size() > 6 ) {
        errors.push_back( where + "invalid MAXLEN \"" + max_length + "\"" );
        continue;
      }
      rule.max_length = strtoul( max_length.c_str(), nullptr, 10 );
      if ( rule.max_length == 0 || rule.max_length > MAX_OSC_LENGTH ) {
        errors.push_back( where + "MAXLEN must be between 1 and " + std::to_string( MAX_OSC_LENGTH ) );
        continue;
      }
    }

    if ( policy.rules.size() == MAX_RULES ) {
      errors.push_back( where + "too many rules (at most " + std::to_string( MAX_RULES ) + ")" );
      continue;
    }
    policy.rules.push_back( rule );
  }

  return policy;
}

std::string OSCPolicy::encode_hex( const std::string& text )
{
  static const char digits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve( text.size() * 2 );
  for ( unsigned char c : text ) {
    hex.push_back( digits[c >> 4] );
    hex.push_back( digits[c & 0xf] );
  }
  return hex;
}

static int hex_value( char c )
{
  if ( c >= '0' && c <= '9' ) {
    return c - '0';
  }
  if ( c >= 'a' && c <= 'f' ) {
    return c - 'a' + 10;
  }
  if ( c >= 'A' && c <= 'F' ) {
    return c - 'A' + 10;
  }
  return -1;
}

bool OSCPolicy::decode_hex( const std::string& hex, std::string& text )
{
  if ( hex.size() % 2 != 0 ) {
    return false;
  }
  std::string decoded;
  decoded.reserve( hex.size() / 2 );
  for ( size_t i = 0; i < hex.size(); i += 2 ) {
    const int high = hex_value( hex[i] );
    const int low = hex_value( hex[i + 1] );
    if ( high < 0 || low < 0 ) {
      return false;
    }
    decoded.push_back( static_cast<char>( ( high << 4 ) | low ) );
  }
  text = decoded;
  return true;
}

int OSCPolicy::match( const std::vector<wchar_t>& osc_string ) const
{
  size_t digits = 0;
  while ( digits < osc_string.size() && osc_string[digits] >= L'0' && osc_string[digits] <= L'9' ) {
    digits++;
  }
  if ( digits == 0 || ( digits < osc_string.size() && osc_string[digits] != L';' ) ) {
    return -1;
  }
  const std::wstring number( osc_string.begin(), osc_string.begin() + digits );
  const size_t text_start = std::min( digits + 1, osc_string.size() );
  const size_t text_length = osc_string.size() - text_start;

  for ( size_t i = 0; i < rules.size(); i++ ) {
    const Rule& rule = rules[i];
    if ( rule.number == number && rule.prefix.size() <= text_length
         && std::equal( rule.prefix.begin(), rule.prefix.end(), osc_string.begin() + text_start ) ) {
      return static_cast<int>( i );
    }
  }
  return -1;
}

/* Mirrors the sequences that Dispatcher::OSC_dispatch acts on. */
bool OSCPolicy::is_native( const std::vector<wchar_t>& osc_string )
{
  static const wchar_t clipboard[] = L"52;c;";
  if ( osc_string.size() >= 5 && std::equal( clipboard, clipboard + 5, osc_string.begin() ) ) {
    return true;
  }
  if ( !osc_string.empty() && osc_string[0] == L';' ) {
    return true;
  }
  if ( osc_string.size() >= 2 && osc_string[1] == L';' ) {
    const wchar_t c = osc_string[0];
    return c == L'0' || c == L'1' || c == L'2' || c == L'8';
  }
  return false;
}

static OSCPolicy& active_policy( void )
{
  static OSCPolicy policy;
  return policy;
}

const OSCPolicy& OSCPolicy::active( void )
{
  return active_policy();
}

void OSCPolicy::set_active( const OSCPolicy& policy )
{
  active_policy() = policy;
}
