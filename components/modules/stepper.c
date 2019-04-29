
#include "module.h"
#include "lauxlib.h"
#include "lmem.h"
#include "platform.h"

#include <string.h>

#define STEPPER_FADE_IN  1
#define STEPPER_FADE_OUT 0
#define SHIFT_LOGICAL  0
#define SHIFT_CIRCULAR 1

#define STEPPER_DEBUG

typedef struct {
  int size;
  uint8_t colorsPerLed;
  uint8_t values[0];
} stepper_buffer;

static void stepper_cleanup( lua_State *L, int pop )
{
  if (pop)
    lua_pop( L, pop );
  platform_stepper_release();
}

// From Lua call:
// stepper.writeSteps( {pin=12, steps=20, dur=30}, {pin=9, steps=40, dur=20}, {pin=12, steps=200, dur=10} )
static int stepper_write_steps( lua_State* L )
{
  int top = lua_gettop( L );

  for (int stack = 1; stack <= top; stack++) {
    if (lua_type( L, stack ) == LUA_TNIL)
      continue;

    if (lua_type( L, stack ) != LUA_TTABLE) {
      stepper_cleanup( L, 0 );
      luaL_checktype( L, stack, LUA_TTABLE ); // trigger error
      return 0;
    }

    //
    // retrieve pin
    //
    lua_getfield( L, stack, "pin" );
    if (!lua_isnumber( L, -1 )) {
      stepper_cleanup( L, 1 );
      return luaL_argerror( L, stack, "invalid pin" );
    }
    int gpio_num = luaL_checkint( L, -1 );
    lua_pop( L, 1 );

    //
    // retrieve duration (10 by default)
    //
    int duration = 10;
    lua_getfield( L, stack, "dur" );
    if (!lua_isnumber( L, -1 )) {
      // duration = 100
      // stepper_cleanup( L, 1 );
      // return luaL_argerror( L, stack, "invalid duration" );
    } else {
      duration = luaL_checkint( L, -1 );
      lua_pop( L, 1 );
    }

    //
    // retrieve steps (1 by default)
    //
    uint32 steps = 1;
    lua_getfield( L, stack, "steps" );
    if (!lua_isnumber( L, -1 )) {
      // duration = 100
      // stepper_cleanup( L, 1 );
      // return luaL_argerror( L, stack, "invalid duration" );
    } else {
      steps = luaL_checkint( L, -1 );
      lua_pop( L, 1 );
    }

    // create the data based on # of steps
    // we just generate an array of bytes of 1's and we 0 pad the last byte
    int byteCnt = steps / 8;
    // int leftoverBitCnt = 0;
    int bytePadCnt = 0;
    if (steps % 8 != 0) {
        // we have a byte where we need to pad
        // leftoverBitCnt = steps % 8;
        bytePadCnt = 1;
    }
    // create data
    uint8_t data[byteCnt + bytePadCnt];
    // for (int i = 0; i < byteCnt; i++) {
    //     data[i] = 0xff;
    // }
    // figure out last padded byte
    // if (bytePadCnt) {
        // there is a padded byte at end
        // data[byteCnt + bytePadCnt - 1] = 0xff; // default to all 1's
        // shift off 8-leftoverBitCnt to get 0's in that place
        // data[byteCnt + bytePadCnt - 1] = data[byteCnt + bytePadCnt - 1] << (8 - leftoverBitCnt);
    // }
    size_t length = byteCnt + bytePadCnt;

#ifdef STEPPER_DEBUG
    // printf("stepper - gpio_num: %d, steps: %d, duration: %d, total bytes: %d\n", gpio_num, steps, duration, byteCnt + bytePadCnt);
#endif

    // prepare channel
    // maybe const is the issue?
    if (platform_stepper_setup( gpio_num, 1, (uint8_t *)data, length, duration, true, steps ) != PLATFORM_OK) {
      stepper_cleanup( L, 0 );
      return luaL_argerror( L, stack, "can't set up chain" );
    }
  }

  //
  // send all channels at once
  //
  if (platform_stepper_send() != PLATFORM_OK) {
    stepper_cleanup( L, 0 );
    return luaL_error( L, "sending failed" );
  }

  stepper_cleanup( L, 0 );

  return 1;
}

// Lua: stepper.write("string")
// Byte triples in the string are interpreted as G R B values.
//
// stepper.write({pin = 4, data = string.char(0, 255, 0)}) sets the first LED red.
// stepper.write({pin = 4, data = string.char(0, 0, 255):rep(10)}) sets ten LEDs blue.
// stepper.write({pin = 4, data = string.char(255, 0, 0, 255, 255, 255)}) first LED green, second LED white.
static int stepper_write( lua_State* L )
{
  int top = lua_gettop( L );

  for (int stack = 1; stack <= top; stack++) {
    if (lua_type( L, stack ) == LUA_TNIL)
      continue;

    if (lua_type( L, stack ) != LUA_TTABLE) {
      stepper_cleanup( L, 0 );
      luaL_checktype( L, stack, LUA_TTABLE ); // trigger error
      return 0;
    }

    //
    // retrieve pin
    //
    lua_getfield( L, stack, "pin" );
    if (!lua_isnumber( L, -1 )) {
      stepper_cleanup( L, 1 );
      return luaL_argerror( L, stack, "invalid pin" );
    }
    int gpio_num = luaL_checkint( L, -1 );
    lua_pop( L, 1 );

    //
    // retrieve duration (100 by default)
    //
    int duration = 100;
    lua_getfield( L, stack, "dur" );
    if (!lua_isnumber( L, -1 )) {
      // duration = 100
      // stepper_cleanup( L, 1 );
      // return luaL_argerror( L, stack, "invalid duration" );
    } else {
      duration = luaL_checkint( L, -1 );
      lua_pop( L, 1 );
    }

    //
    // retrieve data
    //
    lua_getfield( L, stack, "data" );

    const char *data;
    size_t length;
    int type = lua_type( L, -1 );
    if (type == LUA_TSTRING)
    {
      data = lua_tolstring( L, -1, &length );
    }
    else if (type == LUA_TUSERDATA)
    {
      stepper_buffer *buffer = (stepper_buffer*)luaL_checkudata( L, -1, "stepper.buffer" );

      data = (const char *)buffer->values;
      length = buffer->colorsPerLed*buffer->size;
    }
    else
    {
      stepper_cleanup( L, 1 );
      return luaL_argerror(L, stack, "stepper.buffer or string expected");
    }
    lua_pop( L, 1 );

    // prepare channel
    if (platform_stepper_setup( gpio_num, 1, (const uint8_t *)data, length, duration, false, 0 ) != PLATFORM_OK) {
      stepper_cleanup( L, 0 );
      return luaL_argerror( L, stack, "can't set up chain" );
    }
  }

  //
  // send all channels at once
  //
  if (platform_stepper_send() != PLATFORM_OK) {
    stepper_cleanup( L, 0 );
    return luaL_error( L, "sending failed" );
  }

  stepper_cleanup( L, 0 );

  return 0;
}

static ptrdiff_t stepper_posrelat (ptrdiff_t pos, size_t len) {
  /* relative string position: negative means back from end */
  if (pos < 0) pos += (ptrdiff_t)len + 1;
  return (pos >= 0) ? pos : 0;
}

static stepper_buffer *stepper_allocate_buffer(lua_State *L, int leds, int colorsPerLed) {
  // Allocate memory
  size_t size = sizeof(stepper_buffer) + colorsPerLed*leds*sizeof(uint8_t);
  stepper_buffer * buffer = (stepper_buffer*)lua_newuserdata(L, size);

  // Associate its metatable
  luaL_getmetatable(L, "stepper.buffer");
  lua_setmetatable(L, -2);

  // Save led strip size
  buffer->size = leds;
  buffer->colorsPerLed = colorsPerLed;

  return buffer;
}

// Handle a buffer where we can store led values
static int stepper_new_buffer(lua_State *L) {
  const int leds = luaL_checkint(L, 1);
  const int colorsPerLed = luaL_checkint(L, 2);

  luaL_argcheck(L, leds > 0, 1, "should be a positive integer");
  luaL_argcheck(L, colorsPerLed > 0, 2, "should be a positive integer");

  stepper_buffer * buffer = stepper_allocate_buffer(L, leds, colorsPerLed);

  memset(buffer->values, 0, colorsPerLed * leds);

  return 1;
}

static int stepper_buffer_fill(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");

  // Grab colors
  int i, j;
  int * colors = luaM_malloc(L, buffer->colorsPerLed * sizeof(int));

  for (i = 0; i < buffer->colorsPerLed; i++)
  {
    colors[i] = luaL_checkinteger(L, 2+i);
  }

  // Fill buffer
  uint8_t * p = &buffer->values[0];
  for(i = 0; i < buffer->size; i++)
  {
    for (j = 0; j < buffer->colorsPerLed; j++)
    {
      *p++ = colors[j];
    }
  }

  // Free memory
  luaM_free(L, colors);

  return 0;
}

static int stepper_buffer_fade(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");
  const int fade = luaL_checkinteger(L, 2);
  unsigned direction = luaL_optinteger( L, 3, STEPPER_FADE_OUT );

  luaL_argcheck(L, fade > 0, 2, "fade value should be a strict positive number");

  uint8_t * p = &buffer->values[0];
  int val = 0;
  int i;
  for(i = 0; i < buffer->size * buffer->colorsPerLed; i++)
  {
    if (direction == STEPPER_FADE_OUT)
    {
      *p++ /= fade;
    }
    else
    {
      // as fade in can result in value overflow, an int is used to perform the check afterwards
      val = *p * fade;
      if (val > 255) val = 255;
      *p++ = val;
    }
  }

  return 0;
}


static int stepper_buffer_shift(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");
  const int shiftValue = luaL_checkinteger(L, 2);
  const unsigned shift_type = luaL_optinteger( L, 3, SHIFT_LOGICAL );

  ptrdiff_t start = stepper_posrelat(luaL_optinteger(L, 4, 1), buffer->size);
  ptrdiff_t end = stepper_posrelat(luaL_optinteger(L, 5, -1), buffer->size);
  if (start < 1) start = 1;
  if (end > (ptrdiff_t)buffer->size) end = (ptrdiff_t)buffer->size;

  start--;
  int size = end - start;
  size_t offset = start * buffer->colorsPerLed;

  luaL_argcheck(L, shiftValue > 0-size && shiftValue < size, 2, "shifting more elements than buffer size");

  int shift = shiftValue >= 0 ? shiftValue : -shiftValue;

  // check if we want to shift at all
  if (shift == 0 || size <= 0)
  {
    return 0;
  }

  uint8_t * tmp_pixels = luaM_malloc(L, buffer->colorsPerLed * sizeof(uint8_t) * shift);
  size_t shift_len, remaining_len;
  // calculate length of shift section and remaining section
  shift_len = shift*buffer->colorsPerLed;
  remaining_len = (size-shift)*buffer->colorsPerLed;

  if (shiftValue > 0)
  {
    // Store the values which are moved out of the array (last n pixels)
    memcpy(tmp_pixels, &buffer->values[offset + (size-shift)*buffer->colorsPerLed], shift_len);
    // Move pixels to end
    memmove(&buffer->values[offset + shift*buffer->colorsPerLed], &buffer->values[offset], remaining_len);
    // Fill beginning with temp data
    if (shift_type == SHIFT_LOGICAL)
    {
      memset(&buffer->values[offset], 0, shift_len);
    }
    else
    {
      memcpy(&buffer->values[offset], tmp_pixels, shift_len);
    }
  }
  else
  {
    // Store the values which are moved out of the array (last n pixels)
    memcpy(tmp_pixels, &buffer->values[offset], shift_len);
    // Move pixels to end
    memmove(&buffer->values[offset], &buffer->values[offset + shift*buffer->colorsPerLed], remaining_len);
    // Fill beginning with temp data
    if (shift_type == SHIFT_LOGICAL)
    {
      memset(&buffer->values[offset + (size-shift)*buffer->colorsPerLed], 0, shift_len);
    }
    else
    {
      memcpy(&buffer->values[offset + (size-shift)*buffer->colorsPerLed], tmp_pixels, shift_len);
    }
  }
  // Free memory
  luaM_free(L, tmp_pixels);

  return 0;
}

static int stepper_buffer_dump(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");

  lua_pushlstring(L, (char *)buffer->values, buffer->size * buffer->colorsPerLed);

  return 1;
}

static int stepper_buffer_replace(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");
  size_t l = buffer->size;
  ptrdiff_t start = stepper_posrelat(luaL_optinteger(L, 3, 1), l);

  uint8_t *src;
  size_t srcLen;

  if (lua_type(L, 2) == LUA_TSTRING) {
    size_t length;

    src = (uint8_t *) lua_tolstring(L, 2, &length);
    srcLen = length / buffer->colorsPerLed;
  } else {
    stepper_buffer * rhs = (stepper_buffer*)luaL_checkudata(L, 2, "stepper.buffer");
    src = rhs->values;
    srcLen = rhs->size;
    luaL_argcheck(L, rhs->colorsPerLed == buffer->colorsPerLed, 2, "Buffers have different colors");
  }

  luaL_argcheck(L, srcLen + start - 1 <= buffer->size, 2, "Does not fit into destination");

  memcpy(buffer->values + (start - 1) * buffer->colorsPerLed, src, srcLen * buffer->colorsPerLed);

  return 0;
}

// buffer:mix(factor1, buffer1, ..)
// factor is 256 for 100%
// uses saturating arithmetic (one buffer at a time)
static int stepper_buffer_mix(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");

  int pos = 2;
  size_t cells = buffer->size * buffer->colorsPerLed;

  int n_sources = (lua_gettop(L) - 1) / 2;

  struct {
    int factor;
    const uint8_t *values;
  } source[n_sources];

  int src;
  for (src = 0; src < n_sources; src++, pos += 2) {
    int factor = luaL_checkinteger(L, pos);
    stepper_buffer *src_buffer = (stepper_buffer*) luaL_checkudata(L, pos + 1, "stepper.buffer");

    luaL_argcheck(L, src_buffer->size == buffer->size && src_buffer->colorsPerLed == buffer->colorsPerLed, pos + 1, "Buffer not same shape");
    
    source[src].factor = factor;
    source[src].values = src_buffer->values;
  }

  size_t i;
  for (i = 0; i < cells; i++) {
    int val = 0;
    for (src = 0; src < n_sources; src++) {
      val += ((int)(source[src].values[i] * source[src].factor) >> 8);
    }

    if (val < 0) {
      val = 0;
    } else if (val > 255) {
      val = 255;
    }
    buffer->values[i] = val;
  }

  return 0;
}

// Returns the total of all channels
static int stepper_buffer_power(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");

  size_t cells = buffer->size * buffer->colorsPerLed;

  size_t i;
  int total = 0;
  for (i = 0; i < cells; i++) {
    total += buffer->values[i];
  }

  lua_pushnumber(L, total);

  return 1;
}

static int stepper_buffer_get(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");
  const int led = luaL_checkinteger(L, 2) - 1;

  luaL_argcheck(L, led >= 0 && led < buffer->size, 2, "index out of range");

  int i;
  for (i = 0; i < buffer->colorsPerLed; i++)
  {
    lua_pushnumber(L, buffer->values[buffer->colorsPerLed*led+i]);
  }

  return buffer->colorsPerLed;
}

static int stepper_buffer_set(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");
  const int led = luaL_checkinteger(L, 2) - 1;

  luaL_argcheck(L, led >= 0 && led < buffer->size, 2, "index out of range");

  int type = lua_type(L, 3);
  if(type == LUA_TTABLE)
  {
    int i;
    for (i = 0; i < buffer->colorsPerLed; i++)
    {
      // Get value and push it on stack
      lua_rawgeti(L, 3, i+1);

      // Convert it as int and store them in buffer
      buffer->values[buffer->colorsPerLed*led+i] = lua_tonumber(L, -1);
    }

    // Clean up the stack
    lua_pop(L, buffer->colorsPerLed);
  }
  else if(type == LUA_TSTRING)
  {
    size_t len;
    const char * buf = lua_tolstring(L, 3, &len);

    // Overflow check
    if( buffer->colorsPerLed*led + len > buffer->colorsPerLed*buffer->size )
    {
	return luaL_error(L, "string size will exceed strip length");
    }

    memcpy(&buffer->values[buffer->colorsPerLed*led], buf, len);
  }
  else
  {
    int i;
    for (i = 0; i < buffer->colorsPerLed; i++)
    {
      buffer->values[buffer->colorsPerLed*led+i] = luaL_checkinteger(L, 3+i);
    }
  }

  return 0;
}

static int stepper_buffer_size(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");

  lua_pushnumber(L, buffer->size);

  return 1;
}

static int stepper_buffer_sub(lua_State* L) {
  stepper_buffer * lhs = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");
  size_t l = lhs->size;
  ptrdiff_t start = stepper_posrelat(luaL_checkinteger(L, 2), l);
  ptrdiff_t end = stepper_posrelat(luaL_optinteger(L, 3, -1), l);
  if (start < 1) start = 1;
  if (end > (ptrdiff_t)l) end = (ptrdiff_t)l;
  if (start <= end) {
    stepper_buffer *result = stepper_allocate_buffer(L, end - start + 1, lhs->colorsPerLed);
    memcpy(result->values, lhs->values + lhs->colorsPerLed * (start - 1), lhs->colorsPerLed * (end - start + 1));
  } else {
    stepper_buffer *result = stepper_allocate_buffer(L, 0, lhs->colorsPerLed);
    (void)result;
  }
  return 1;
}

static int stepper_buffer_concat(lua_State* L) {
  stepper_buffer * lhs = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");
  stepper_buffer * rhs = (stepper_buffer*)luaL_checkudata(L, 2, "stepper.buffer");

  luaL_argcheck(L, lhs->colorsPerLed == rhs->colorsPerLed, 1, "Can only concatenate buffers with same colors");

  int colorsPerLed = lhs->colorsPerLed;
  int leds = lhs->size + rhs->size;
 
  stepper_buffer * buffer = stepper_allocate_buffer(L, leds, colorsPerLed);

  memcpy(buffer->values, lhs->values, lhs->colorsPerLed * lhs->size);
  memcpy(buffer->values + lhs->colorsPerLed * lhs->size, rhs->values, rhs->colorsPerLed * rhs->size);

  return 1;
}

static int stepper_buffer_tostring(lua_State* L) {
  stepper_buffer * buffer = (stepper_buffer*)luaL_checkudata(L, 1, "stepper.buffer");

  luaL_Buffer result;
  luaL_buffinit(L, &result);

  luaL_addchar(&result, '[');
  int i;
  int p = 0;
  for (i = 0; i < buffer->size; i++) {
    int j;
    if (i > 0) {
      luaL_addchar(&result, ',');
    }
    luaL_addchar(&result, '(');
    for (j = 0; j < buffer->colorsPerLed; j++, p++) {
      if (j > 0) {
        luaL_addchar(&result, ',');
      }
      char numbuf[5];
      sprintf(numbuf, "%d", buffer->values[p]);
      luaL_addstring(&result, numbuf);
    }
    luaL_addchar(&result, ')');
  }

  luaL_addchar(&result, ']');
  luaL_pushresult(&result);

  return 1;
}

static const LUA_REG_TYPE stepper_buffer_map[] =
{
  { LSTRKEY( "dump" ),    LFUNCVAL( stepper_buffer_dump )},
  { LSTRKEY( "fade" ),    LFUNCVAL( stepper_buffer_fade )},
  { LSTRKEY( "fill" ),    LFUNCVAL( stepper_buffer_fill )},
  { LSTRKEY( "get" ),     LFUNCVAL( stepper_buffer_get )},
  { LSTRKEY( "replace" ), LFUNCVAL( stepper_buffer_replace )},
  { LSTRKEY( "mix" ),     LFUNCVAL( stepper_buffer_mix )},
  { LSTRKEY( "power" ),   LFUNCVAL( stepper_buffer_power )},
  { LSTRKEY( "set" ),     LFUNCVAL( stepper_buffer_set )},
  { LSTRKEY( "shift" ),   LFUNCVAL( stepper_buffer_shift )},
  { LSTRKEY( "size" ),    LFUNCVAL( stepper_buffer_size )},
  { LSTRKEY( "sub" ),     LFUNCVAL( stepper_buffer_sub )},
  { LSTRKEY( "__concat" ),LFUNCVAL( stepper_buffer_concat )},
  { LSTRKEY( "__index" ), LROVAL( stepper_buffer_map )},
  { LSTRKEY( "__tostring" ), LFUNCVAL( stepper_buffer_tostring )},
  { LNILKEY, LNILVAL}
};

static const LUA_REG_TYPE stepper_map[] =
{
  { LSTRKEY( "newBuffer" ),      LFUNCVAL( stepper_new_buffer )},
  { LSTRKEY( "write" ),          LFUNCVAL( stepper_write )},
  { LSTRKEY( "writeSteps" ),          LFUNCVAL( stepper_write_steps )},
  { LSTRKEY( "STEPPER_FADE_IN" ),        LNUMVAL( STEPPER_FADE_IN ) },
  { LSTRKEY( "STEPPER_FADE_OUT" ),       LNUMVAL( STEPPER_FADE_OUT ) },
  { LSTRKEY( "STEPPER_SHIFT_LOGICAL" ),  LNUMVAL( SHIFT_LOGICAL ) },
  { LSTRKEY( "STEPPER_SHIFT_CIRCULAR" ), LNUMVAL( SHIFT_CIRCULAR ) },
  { LNILKEY, LNILVAL}
};

int luaopen_stepper(lua_State *L) {
  // TODO: Make sure that the GPIO system is initialized
  luaL_rometatable(L, "stepper.buffer", (void *)stepper_buffer_map);  // create metatable for stepper.buffer
  return 0;
}

NODEMCU_MODULE(STEPPER, "stepper", stepper_map, luaopen_stepper);
