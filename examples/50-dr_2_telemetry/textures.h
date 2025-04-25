#pragma once

#ifndef TEXTURES_H
#define TEXUTRES_H

namespace tex {

	#ifndef _VEC_2
		struct uv {
			float u = 0;
			float v = 0;
		};

	#define _VEC_2(u, v) ( tex::uv { u, v } )
	#endif

	#define MAIN_TEX_WIDTH 512
	#define MAIN_TEX_HEIGHT 512

	#define DIM 512.f

	#define GFORCE_DIM			150		
	#define GFORCE_BG_UV0()		(_VEC_2(44.f  / DIM,	0.f))
	#define GFORCE_BG_UV1()		(_VEC_2(193.f / DIM,	149.f / DIM))
	#define GFORCE_IND_DIM		10
	#define GFORCE_IND_UV0()	(_VEC_2( 196.f / DIM,	10.f / DIM))
	#define GFORCE_IND_UV1()	(_VEC_2( 205.f / DIM,	0.f / DIM))

	#define THROTTLE_V_SIZE		(_VEC_2(20.f, 150.f ) )
	#define THROTTLE_V_UV0()	(_VEC_2(0.f,			149.f / DIM) )
	#define THROTTLE_V_UV1()	(_VEC_2(19.f / DIM,		0.f / DIM) )

	#define THROTTLE_H_SIZE()	(_VEC_2(150.f, 20.f) )
	#define THROTTLE_H_UV0()	(_VEC_2(0.f,			171.f / DIM) )
	#define THROTTLE_H_UV1()	(_VEC_2(149.f / DIM,	152.f / DIM) )

	#define BRAKE_V_SIZE		(_VEC_2(20.f, 150.f ))
	#define BRAKE_V_UV0()		(_VEC_2(22.f / DIM,		149.f / DIM))
	#define BRAKE_V_UV1()		(_VEC_2(41.f / DIM,		0.f / DIM))

	#define BRAKE_H_SIZE		(_VEC_2(150.f, 20.f))
	#define BRAKE_H_UV0()		(_VEC_2(0.f,			193.f / DIM))
	#define BRAKE_H_UV1()		(_VEC_2(149.f / DIM,	174.f / DIM))

	#define BLOCK_DIM			20
	#define BLOCK_RED_UV0()		(_VEC_2(0.f,			215.f / DIM))
	#define BLOCK_RED_UV1()		(_VEC_2(19.f / DIM,		196.f / DIM))

	#define BLOCK_GREEN_UV0()	(_VEC_2(22.f / DIM,		215.f / DIM))
	#define BLOCK_GREEN_UV1()	(_VEC_2(41.f / DIM,		196.f / DIM))

	#define BLOCK_BLUE_UV0()	(_VEC_2(44.f / DIM,		215.f / DIM))
	#define BLOCK_BLUE_UV1()	(_VEC_2(63.f / DIM,		196.f / DIM))

}

#endif // TEXTURES_H
