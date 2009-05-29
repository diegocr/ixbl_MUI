
#include <exec/types.h>
#include <libraries/mui.h>
#include "ixblogo.h"

GLOBAL Object *MUI_NewObject( STRPTR, Tag, ...);

Object * IXBLogo ( VOID )
{
	Object * obj;
	
	obj = BodychunkObject,
		MUIA_Group_Spacing         , 0,
		MUIA_FixWidth              , IXBLOGO_WIDTH ,
		MUIA_FixHeight             , IXBLOGO_HEIGHT,
		MUIA_Bitmap_Width          , IXBLOGO_WIDTH ,
		MUIA_Bitmap_Height         , IXBLOGO_HEIGHT,
		MUIA_Bodychunk_Depth       , IXBLOGO_DEPTH ,
		MUIA_Bodychunk_Body        , (UBYTE *) ixblogo_body,
		MUIA_Bodychunk_Compression , IXBLOGO_COMPRESSION,
		MUIA_Bodychunk_Masking     , IXBLOGO_MASKING,
		MUIA_Bitmap_SourceColors   , (ULONG *) ixblogo_colors,
		MUIA_Bitmap_Transparent    , 0,
		MUIA_Bitmap_Precision      , PRECISION_EXACT,
	End;
	
	return obj;
}
