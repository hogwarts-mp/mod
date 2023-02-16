// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/ObjectThumbnail.h"
#include "Serialization/StructuredArchive.h"

/** Static: Thumbnail compressor */
FThumbnailCompressionInterface* FObjectThumbnail::ThumbnailCompressor = NULL;


FObjectThumbnail::FObjectThumbnail()
	: ImageWidth( 0 ),
	  ImageHeight( 0 ),
	  bIsDirty( false ),
	  bLoadedFromDisk(false),
	  bCreatedAfterCustomThumbForSharedTypesEnabled(false)
{ }


const TArray< uint8 >& FObjectThumbnail::GetUncompressedImageData() const
{
	if( ImageData.Num() == 0 )
	{
		// Const cast here so that we can populate the image data on demand	(write once)
		FObjectThumbnail* MutableThis = const_cast< FObjectThumbnail* >( this );
		MutableThis->DecompressImageData();
	}
	return ImageData;
}


void FObjectThumbnail::Serialize( FArchive& Ar )
{
	Serialize(FStructuredArchiveFromArchive(Ar).GetSlot());
}

void FObjectThumbnail::Serialize(FStructuredArchive::FSlot Slot)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	Record << SA_VALUE(TEXT("ImageWidth"), ImageWidth);
	Record << SA_VALUE(TEXT("ImageHeight"), ImageHeight);

	//if the image thinks it's empty, ensure there is no memory waste
	if ((ImageWidth == 0) || (ImageHeight == 0))
	{
		CompressedImageData.Reset();
	}

	// Compress the image on demand if we don't have any compressed bytes yet.
	if (CompressedImageData.Num() == 0 &&
		(Slot.GetUnderlyingArchive().IsSaving() || Slot.GetUnderlyingArchive().IsCountingMemory()))
	{
		CompressImageData();
	}

	Record << SA_VALUE(TEXT("CompressedImageData"), CompressedImageData);

	if (Slot.GetUnderlyingArchive().IsCountingMemory())
	{
		Record << SA_VALUE(TEXT("ImageData"), ImageData) << SA_VALUE(TEXT("bIsDirty"), bIsDirty);
	}

	if (Slot.GetArchiveState().IsLoading())
	{
		bLoadedFromDisk = true;
		if ((ImageWidth>0) && (ImageHeight>0))
		{
			bCreatedAfterCustomThumbForSharedTypesEnabled = true;
		}
	}
}


void FObjectThumbnail::CompressImageData()
{
	CompressedImageData.Reset();
	if( ThumbnailCompressor != NULL && ImageData.Num() > 0 && ImageWidth > 0 && ImageHeight > 0 )
	{
		ThumbnailCompressor->CompressImage( ImageData, ImageWidth, ImageHeight, CompressedImageData );
	}
}


void FObjectThumbnail::DecompressImageData()
{
	ImageData.Reset();
	if( ThumbnailCompressor != NULL && CompressedImageData.Num() > 0 && ImageWidth > 0 && ImageHeight > 0 )
	{
		ThumbnailCompressor->DecompressImage( CompressedImageData, ImageWidth, ImageHeight, ImageData );
	}
}


void FObjectThumbnail::CountBytes( FArchive& Ar ) const
{
	SIZE_T StaticSize = sizeof(FObjectThumbnail);
	Ar.CountBytes(StaticSize, Align(StaticSize, alignof(FObjectThumbnail)));

	FObjectThumbnail* UnconstThis = const_cast<FObjectThumbnail*>(this);
	UnconstThis->CompressedImageData.CountBytes(Ar);
	UnconstThis->ImageData.CountBytes(Ar);
}


void FObjectThumbnail::CountImageBytes_Compressed( FArchive& Ar ) const
{
	const_cast<FObjectThumbnail*>(this)->CompressedImageData.CountBytes(Ar);
}


void FObjectThumbnail::CountImageBytes_Uncompressed( FArchive& Ar ) const
{
	const_cast<FObjectThumbnail*>(this)->ImageData.CountBytes(Ar);
}


void FObjectFullNameAndThumbnail::CountBytes( FArchive& Ar ) const
{
	SIZE_T StaticSize = sizeof(FObjectFullNameAndThumbnail);
	Ar.CountBytes(StaticSize, Align(StaticSize, alignof(FObjectFullNameAndThumbnail)));

	if ( ObjectThumbnail != NULL )
	{
		ObjectThumbnail->CountBytes(Ar);
	}
}
