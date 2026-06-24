// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/VarInt.h"

#include "Misc/AutomationTest.h"
#include "Serialization/BufferReader.h"
#include "Serialization/BufferWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVarIntMeasureTest, "System.Core.Serialization.VarInt.Measure", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FVarIntMeasureTest::RunTest(const FString& Parameters)
{
	// Test MeasureVarInt at signed 32-bit encoding boundaries.
	TestEqual(TEXT("MeasureVarInt(0x0000'0000)"), MeasureVarInt(int32(0x0000'0000)), 1);
	TestEqual(TEXT("MeasureVarInt(0x0000'0001)"), MeasureVarInt(int32(0x0000'0001)), 1);
	TestEqual(TEXT("MeasureVarInt(0x0000'003f)"), MeasureVarInt(int32(0x0000'003f)), 1);
	TestEqual(TEXT("MeasureVarInt(0x0000'0040)"), MeasureVarInt(int32(0x0000'0040)), 2);
	TestEqual(TEXT("MeasureVarInt(0x0000'1fff)"), MeasureVarInt(int32(0x0000'1fff)), 2);
	TestEqual(TEXT("MeasureVarInt(0x0000'2000)"), MeasureVarInt(int32(0x0000'2000)), 3);
	TestEqual(TEXT("MeasureVarInt(0x000f'ffff)"), MeasureVarInt(int32(0x000f'ffff)), 3);
	TestEqual(TEXT("MeasureVarInt(0x0010'0000)"), MeasureVarInt(int32(0x0010'0000)), 4);
	TestEqual(TEXT("MeasureVarInt(0x07ff'ffff)"), MeasureVarInt(int32(0x07ff'ffff)), 4);
	TestEqual(TEXT("MeasureVarInt(0x0800'0000)"), MeasureVarInt(int32(0x0800'0000)), 5);
	TestEqual(TEXT("MeasureVarInt(0x7fff'ffff)"), MeasureVarInt(int32(0x7fff'ffff)), 5);
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff)"), MeasureVarInt(int32(0xffff'ffff)), 1); // -0x0000'0001
	TestEqual(TEXT("MeasureVarInt(0xffff'ffc0)"), MeasureVarInt(int32(0xffff'ffc0)), 1); // -0x0000'0040
	TestEqual(TEXT("MeasureVarInt(0xffff'ffbf)"), MeasureVarInt(int32(0xffff'ffbf)), 2); // -0x0000'0041
	TestEqual(TEXT("MeasureVarInt(0xffff'e000)"), MeasureVarInt(int32(0xffff'e000)), 2); // -0x0000'2000
	TestEqual(TEXT("MeasureVarInt(0xffff'dfff)"), MeasureVarInt(int32(0xffff'dfff)), 3); // -0x0000'2001
	TestEqual(TEXT("MeasureVarInt(0xfff0'0000)"), MeasureVarInt(int32(0xfff0'0000)), 3); // -0x0010'0000
	TestEqual(TEXT("MeasureVarInt(0xffef'ffff)"), MeasureVarInt(int32(0xffef'ffff)), 4); // -0x0010'0001
	TestEqual(TEXT("MeasureVarInt(0xf800'0000)"), MeasureVarInt(int32(0xf800'0000)), 4); // -0x0800'0000
	TestEqual(TEXT("MeasureVarInt(0xf7ff'ffff)"), MeasureVarInt(int32(0xf7ff'ffff)), 5); // -0x0800'0001
	TestEqual(TEXT("MeasureVarInt(0x8000'0000)"), MeasureVarInt(int32(0x8000'0000)), 5); // -0x8000'0000

	// Test MeasureVarUInt at unsigned 32-bit encoding boundaries.
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000)"), MeasureVarUInt(uint32(0x0000'0000)), 1);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0001)"), MeasureVarUInt(uint32(0x0000'0001)), 1);
	TestEqual(TEXT("MeasureVarUInt(0x0000'007f)"), MeasureVarUInt(uint32(0x0000'007f)), 1);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0080)"), MeasureVarUInt(uint32(0x0000'0080)), 2);
	TestEqual(TEXT("MeasureVarUInt(0x0000'3fff)"), MeasureVarUInt(uint32(0x0000'3fff)), 2);
	TestEqual(TEXT("MeasureVarUInt(0x0000'4000)"), MeasureVarUInt(uint32(0x0000'4000)), 3);
	TestEqual(TEXT("MeasureVarUInt(0x001f'ffff)"), MeasureVarUInt(uint32(0x001f'ffff)), 3);
	TestEqual(TEXT("MeasureVarUInt(0x0020'0000)"), MeasureVarUInt(uint32(0x0020'0000)), 4);
	TestEqual(TEXT("MeasureVarUInt(0x0fff'ffff)"), MeasureVarUInt(uint32(0x0fff'ffff)), 4);
	TestEqual(TEXT("MeasureVarUInt(0x1000'0000)"), MeasureVarUInt(uint32(0x1000'0000)), 5);
	TestEqual(TEXT("MeasureVarUInt(0xffff'ffff)"), MeasureVarUInt(uint32(0xffff'ffff)), 5);

	// Test MeasureVarInt at signed 64-bit encoding boundaries.
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'0000'0000)"), MeasureVarInt(int64(0x0000'0000'0000'0000)), 1);
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'0000'0001)"), MeasureVarInt(int64(0x0000'0000'0000'0001)), 1);
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'0000'003f)"), MeasureVarInt(int64(0x0000'0000'0000'003f)), 1);
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'0000'0040)"), MeasureVarInt(int64(0x0000'0000'0000'0040)), 2);
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'0000'1fff)"), MeasureVarInt(int64(0x0000'0000'0000'1fff)), 2);
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'0000'2000)"), MeasureVarInt(int64(0x0000'0000'0000'2000)), 3);
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'000f'ffff)"), MeasureVarInt(int64(0x0000'0000'000f'ffff)), 3);
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'0010'0000)"), MeasureVarInt(int64(0x0000'0000'0010'0000)), 4);
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'07ff'ffff)"), MeasureVarInt(int64(0x0000'0000'07ff'ffff)), 4);
	TestEqual(TEXT("MeasureVarInt(0x0000'0000'0800'0000)"), MeasureVarInt(int64(0x0000'0000'0800'0000)), 5);
	TestEqual(TEXT("MeasureVarInt(0x0000'0003'ffff'ffff)"), MeasureVarInt(int64(0x0000'0003'ffff'ffff)), 5);
	TestEqual(TEXT("MeasureVarInt(0x0000'0004'0000'0000)"), MeasureVarInt(int64(0x0000'0004'0000'0000)), 6);
	TestEqual(TEXT("MeasureVarInt(0x0000'01ff'ffff'ffff)"), MeasureVarInt(int64(0x0000'01ff'ffff'ffff)), 6);
	TestEqual(TEXT("MeasureVarInt(0x0000'0200'0000'0000)"), MeasureVarInt(int64(0x0000'0200'0000'0000)), 7);
	TestEqual(TEXT("MeasureVarInt(0x0000'ffff'ffff'ffff)"), MeasureVarInt(int64(0x0000'ffff'ffff'ffff)), 7);
	TestEqual(TEXT("MeasureVarInt(0x0001'0000'0000'0000)"), MeasureVarInt(int64(0x0001'0000'0000'0000)), 8);
	TestEqual(TEXT("MeasureVarInt(0x007f'ffff'ffff'ffff)"), MeasureVarInt(int64(0x007f'ffff'ffff'ffff)), 8);
	TestEqual(TEXT("MeasureVarInt(0x0080'0000'0000'0000)"), MeasureVarInt(int64(0x0080'0000'0000'0000)), 9);
	TestEqual(TEXT("MeasureVarInt(0x7fff'ffff'ffff'ffff)"), MeasureVarInt(int64(0x7fff'ffff'ffff'ffff)), 9);
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff'ffff'ffff)"), MeasureVarInt(int64(0xffff'ffff'ffff'ffff)), 1); // -0x0000'0000'0000'0001
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff'ffff'ffc0)"), MeasureVarInt(int64(0xffff'ffff'ffff'ffc0)), 1); // -0x0000'0000'0000'0040
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff'ffff'ffbf)"), MeasureVarInt(int64(0xffff'ffff'ffff'ffbf)), 2); // -0x0000'0000'0000'0041
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff'ffff'e000)"), MeasureVarInt(int64(0xffff'ffff'ffff'e000)), 2); // -0x0000'0000'0000'2000
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff'ffff'dfff)"), MeasureVarInt(int64(0xffff'ffff'ffff'dfff)), 3); // -0x0000'0000'0000'2001
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff'fff0'0000)"), MeasureVarInt(int64(0xffff'ffff'fff0'0000)), 3); // -0x0000'0000'0010'0000
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff'ffef'ffff)"), MeasureVarInt(int64(0xffff'ffff'ffef'ffff)), 4); // -0x0000'0000'0010'0001
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff'f800'0000)"), MeasureVarInt(int64(0xffff'ffff'f800'0000)), 4); // -0x0000'0000'0800'0000
	TestEqual(TEXT("MeasureVarInt(0xffff'ffff'f7ff'ffff)"), MeasureVarInt(int64(0xffff'ffff'f7ff'ffff)), 5); // -0x0000'0000'0800'0001
	TestEqual(TEXT("MeasureVarInt(0xffff'fffc'0000'0000)"), MeasureVarInt(int64(0xffff'fffc'0000'0000)), 5); // -0x0000'0004'0000'0000
	TestEqual(TEXT("MeasureVarInt(0xffff'fffb'ffff'ffff)"), MeasureVarInt(int64(0xffff'fffb'ffff'ffff)), 6); // -0x0000'0004'0000'0001
	TestEqual(TEXT("MeasureVarInt(0xffff'fe00'0000'0000)"), MeasureVarInt(int64(0xffff'fe00'0000'0000)), 6); // -0x0000'0200'0000'0000
	TestEqual(TEXT("MeasureVarInt(0xffff'fdff'ffff'ffff)"), MeasureVarInt(int64(0xffff'fdff'ffff'ffff)), 7); // -0x0000'0200'0000'0001
	TestEqual(TEXT("MeasureVarInt(0xffff'0000'0000'0000)"), MeasureVarInt(int64(0xffff'0000'0000'0000)), 7); // -0x0001'0000'0000'0000
	TestEqual(TEXT("MeasureVarInt(0xfffe'ffff'ffff'ffff)"), MeasureVarInt(int64(0xfffe'ffff'ffff'ffff)), 8); // -0x0001'0000'0000'0001
	TestEqual(TEXT("MeasureVarInt(0xff80'0000'0000'0000)"), MeasureVarInt(int64(0xff80'0000'0000'0000)), 8); // -0x0080'0000'0000'0000
	TestEqual(TEXT("MeasureVarInt(0xff7f'ffff'ffff'ffff)"), MeasureVarInt(int64(0xff7f'ffff'ffff'ffff)), 9); // -0x0080'0000'0000'0001
	TestEqual(TEXT("MeasureVarInt(0x8000'0000'0000'0000)"), MeasureVarInt(int64(0x8000'0000'0000'0000)), 9); // -0x8000'0000'0000'0000

	// Test MeasureVarUInt at unsigned 64-bit encoding boundaries.
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'0000'0000)"), MeasureVarUInt(uint64(0x0000'0000'0000'0000)), 1);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'0000'0001)"), MeasureVarUInt(uint64(0x0000'0000'0000'0001)), 1);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'0000'007f)"), MeasureVarUInt(uint64(0x0000'0000'0000'007f)), 1);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'0000'0080)"), MeasureVarUInt(uint64(0x0000'0000'0000'0080)), 2);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'0000'3fff)"), MeasureVarUInt(uint64(0x0000'0000'0000'3fff)), 2);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'0000'4000)"), MeasureVarUInt(uint64(0x0000'0000'0000'4000)), 3);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'001f'ffff)"), MeasureVarUInt(uint64(0x0000'0000'001f'ffff)), 3);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'0020'0000)"), MeasureVarUInt(uint64(0x0000'0000'0020'0000)), 4);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'0fff'ffff)"), MeasureVarUInt(uint64(0x0000'0000'0fff'ffff)), 4);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0000'1000'0000)"), MeasureVarUInt(uint64(0x0000'0000'1000'0000)), 5);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0007'ffff'ffff)"), MeasureVarUInt(uint64(0x0000'0007'ffff'ffff)), 5);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0008'0000'0000)"), MeasureVarUInt(uint64(0x0000'0008'0000'0000)), 6);
	TestEqual(TEXT("MeasureVarUInt(0x0000'03ff'ffff'ffff)"), MeasureVarUInt(uint64(0x0000'03ff'ffff'ffff)), 6);
	TestEqual(TEXT("MeasureVarUInt(0x0000'0400'0000'0000)"), MeasureVarUInt(uint64(0x0000'0400'0000'0000)), 7);
	TestEqual(TEXT("MeasureVarUInt(0x0001'ffff'ffff'ffff)"), MeasureVarUInt(uint64(0x0001'ffff'ffff'ffff)), 7);
	TestEqual(TEXT("MeasureVarUInt(0x0002'0000'0000'0000)"), MeasureVarUInt(uint64(0x0002'0000'0000'0000)), 8);
	TestEqual(TEXT("MeasureVarUInt(0x00ff'ffff'ffff'ffff)"), MeasureVarUInt(uint64(0x00ff'ffff'ffff'ffff)), 8);
	TestEqual(TEXT("MeasureVarUInt(0x0100'0000'0000'0000)"), MeasureVarUInt(uint64(0x0100'0000'0000'0000)), 9);
	TestEqual(TEXT("MeasureVarUInt(0xffff'ffff'ffff'ffff)"), MeasureVarUInt(uint64(0xffff'ffff'ffff'ffff)), 9);

	// Test MeasureVarInt at encoding boundaries.
	auto TestMeasureVarInt = [](uint8 FirstByte) { return MeasureVarInt(&FirstByte); };
	TestEqual(TEXT("MeasureVarInt(1-byte array)"), TestMeasureVarInt(0b0000'0000), 1);
	TestEqual(TEXT("MeasureVarInt(1-byte array)"), TestMeasureVarInt(0b0111'1111), 1);
	TestEqual(TEXT("MeasureVarInt(2-byte array)"), TestMeasureVarInt(0b1000'0000), 2);
	TestEqual(TEXT("MeasureVarInt(2-byte array)"), TestMeasureVarInt(0b1011'1111), 2);
	TestEqual(TEXT("MeasureVarInt(3-byte array)"), TestMeasureVarInt(0b1100'0000), 3);
	TestEqual(TEXT("MeasureVarInt(3-byte array)"), TestMeasureVarInt(0b1101'1111), 3);
	TestEqual(TEXT("MeasureVarInt(4-byte array)"), TestMeasureVarInt(0b1110'0000), 4);
	TestEqual(TEXT("MeasureVarInt(4-byte array)"), TestMeasureVarInt(0b1110'1111), 4);
	TestEqual(TEXT("MeasureVarInt(5-byte array)"), TestMeasureVarInt(0b1111'0000), 5);
	TestEqual(TEXT("MeasureVarInt(5-byte array)"), TestMeasureVarInt(0b1111'0111), 5);
	TestEqual(TEXT("MeasureVarInt(6-byte array)"), TestMeasureVarInt(0b1111'1000), 6);
	TestEqual(TEXT("MeasureVarInt(6-byte array)"), TestMeasureVarInt(0b1111'1011), 6);
	TestEqual(TEXT("MeasureVarInt(7-byte array)"), TestMeasureVarInt(0b1111'1100), 7);
	TestEqual(TEXT("MeasureVarInt(7-byte array)"), TestMeasureVarInt(0b1111'1101), 7);
	TestEqual(TEXT("MeasureVarInt(8-byte array)"), TestMeasureVarInt(0b1111'1110), 8);
	TestEqual(TEXT("MeasureVarInt(9-byte array)"), TestMeasureVarInt(0b1111'1111), 9);

	// Test MeasureVarUInt at encoding boundaries.
	auto TestMeasureVarUInt = [](uint8 FirstByte) { return MeasureVarUInt(&FirstByte); };
	TestEqual(TEXT("MeasureVarUInt(1-byte array)"), TestMeasureVarUInt(0b0000'0000), 1);
	TestEqual(TEXT("MeasureVarUInt(1-byte array)"), TestMeasureVarUInt(0b0111'1111), 1);
	TestEqual(TEXT("MeasureVarUInt(2-byte array)"), TestMeasureVarUInt(0b1000'0000), 2);
	TestEqual(TEXT("MeasureVarUInt(2-byte array)"), TestMeasureVarUInt(0b1011'1111), 2);
	TestEqual(TEXT("MeasureVarUInt(3-byte array)"), TestMeasureVarUInt(0b1100'0000), 3);
	TestEqual(TEXT("MeasureVarUInt(3-byte array)"), TestMeasureVarUInt(0b1101'1111), 3);
	TestEqual(TEXT("MeasureVarUInt(4-byte array)"), TestMeasureVarUInt(0b1110'0000), 4);
	TestEqual(TEXT("MeasureVarUInt(4-byte array)"), TestMeasureVarUInt(0b1110'1111), 4);
	TestEqual(TEXT("MeasureVarUInt(5-byte array)"), TestMeasureVarUInt(0b1111'0000), 5);
	TestEqual(TEXT("MeasureVarUInt(5-byte array)"), TestMeasureVarUInt(0b1111'0111), 5);
	TestEqual(TEXT("MeasureVarUInt(6-byte array)"), TestMeasureVarUInt(0b1111'1000), 6);
	TestEqual(TEXT("MeasureVarUInt(6-byte array)"), TestMeasureVarUInt(0b1111'1011), 6);
	TestEqual(TEXT("MeasureVarUInt(7-byte array)"), TestMeasureVarUInt(0b1111'1100), 7);
	TestEqual(TEXT("MeasureVarUInt(7-byte array)"), TestMeasureVarUInt(0b1111'1101), 7);
	TestEqual(TEXT("MeasureVarUInt(8-byte array)"), TestMeasureVarUInt(0b1111'1110), 8);
	TestEqual(TEXT("MeasureVarUInt(9-byte array)"), TestMeasureVarUInt(0b1111'1111), 9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVarIntSerializeTest, "System.Core.Serialization.VarInt.Serialize", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FVarIntSerializeTest::RunTest(const FString& Parameters)
{
	// Test Read/WriteVarInt at signed 32-bit encoding boundaries.
	auto TestSerializeVarInt32 = [](int32 Value) -> bool
	{
		uint8 Buffer[5];
		const uint32 WriteByteCount = WriteVarInt(Value, Buffer);
		uint32 ReadByteCount = 0;
		const bool bBufferPass = WriteByteCount <= 5 && ReadVarInt(Buffer, ReadByteCount) == Value && ReadByteCount == WriteByteCount;

		uint8 ArBuffer[5];
		FBufferWriter WriteAr(ArBuffer, 5);
		WriteVarIntToArchive(WriteAr, Value);
		FBufferReader ReadAr(ArBuffer, 5, /*bFreeOnClose*/ false);
		const bool bArchivePass = WriteAr.Tell() == WriteByteCount && ReadVarIntFromArchive(ReadAr) == Value && ReadAr.Tell() == ReadByteCount;

		return bBufferPass && bArchivePass;
	};
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000)"), TestSerializeVarInt32(int32(0x0000'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0001)"), TestSerializeVarInt32(int32(0x0000'0001)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'003f)"), TestSerializeVarInt32(int32(0x0000'003f)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0040)"), TestSerializeVarInt32(int32(0x0000'0040)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'1fff)"), TestSerializeVarInt32(int32(0x0000'1fff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'2000)"), TestSerializeVarInt32(int32(0x0000'2000)));
	TestTrue(TEXT("Read/WriteVarInt(0x000f'ffff)"), TestSerializeVarInt32(int32(0x000f'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0010'0000)"), TestSerializeVarInt32(int32(0x0010'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x07ff'ffff)"), TestSerializeVarInt32(int32(0x07ff'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0800'0000)"), TestSerializeVarInt32(int32(0x0800'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x7fff'ffff)"), TestSerializeVarInt32(int32(0x7fff'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff)"), TestSerializeVarInt32(int32(0xffff'ffff))); // -0x0000'0001
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffc0)"), TestSerializeVarInt32(int32(0xffff'ffc0))); // -0x0000'0040
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffbf)"), TestSerializeVarInt32(int32(0xffff'ffbf))); // -0x0000'0041
	TestTrue(TEXT("Read/WriteVarInt(0xffff'e000)"), TestSerializeVarInt32(int32(0xffff'e000))); // -0x0000'2000
	TestTrue(TEXT("Read/WriteVarInt(0xffff'dfff)"), TestSerializeVarInt32(int32(0xffff'dfff))); // -0x0000'2001
	TestTrue(TEXT("Read/WriteVarInt(0xfff0'0000)"), TestSerializeVarInt32(int32(0xfff0'0000))); // -0x0010'0000
	TestTrue(TEXT("Read/WriteVarInt(0xffef'ffff)"), TestSerializeVarInt32(int32(0xffef'ffff))); // -0x0010'0001
	TestTrue(TEXT("Read/WriteVarInt(0xf800'0000)"), TestSerializeVarInt32(int32(0xf800'0000))); // -0x0800'0000
	TestTrue(TEXT("Read/WriteVarInt(0xf7ff'ffff)"), TestSerializeVarInt32(int32(0xf7ff'ffff))); // -0x0800'0001
	TestTrue(TEXT("Read/WriteVarInt(0x8000'0000)"), TestSerializeVarInt32(int32(0x8000'0000))); // -0x8000'0000

	// Test Read/WriteVarUInt at unsigned 32-bit encoding boundaries.
	auto TestSerializeVarUInt32 = [](uint32 Value) -> bool
	{
		uint8 Buffer[5];
		const uint32 WriteByteCount = WriteVarUInt(Value, Buffer);
		uint32 ReadByteCount = 0;
		const bool bBufferPass = WriteByteCount <= 5 && ReadVarUInt(Buffer, ReadByteCount) == Value && ReadByteCount == WriteByteCount;

		uint8 ArBuffer[5];
		FBufferWriter WriteAr(ArBuffer, 5);
		WriteVarUIntToArchive(WriteAr, Value);
		FBufferReader ReadAr(ArBuffer, 5, /*bFreeOnClose*/ false);
		const bool bArchivePass = WriteAr.Tell() == WriteByteCount && ReadVarUIntFromArchive(ReadAr) == Value && ReadAr.Tell() == ReadByteCount;

		return bBufferPass && bArchivePass;
	};
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000)"), TestSerializeVarUInt32(uint32(0x0000'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'007f)"), TestSerializeVarUInt32(uint32(0x0000'007f)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0080)"), TestSerializeVarUInt32(uint32(0x0000'0080)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'3fff)"), TestSerializeVarUInt32(uint32(0x0000'3fff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'4000)"), TestSerializeVarUInt32(uint32(0x0000'4000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'7fff)"), TestSerializeVarUInt32(uint32(0x0000'7fff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'8000)"), TestSerializeVarUInt32(uint32(0x0000'8000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'ffff)"), TestSerializeVarUInt32(uint32(0x0000'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x001f'ffff)"), TestSerializeVarUInt32(uint32(0x001f'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0020'0000)"), TestSerializeVarUInt32(uint32(0x0020'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0fff'ffff)"), TestSerializeVarUInt32(uint32(0x0fff'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x1000'0000)"), TestSerializeVarUInt32(uint32(0x1000'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0xffff'ffff)"), TestSerializeVarUInt32(uint32(0xffff'ffff)));

	// Test Read/WriteVarInt at signed 64-bit encoding boundaries.
	auto TestSerializeVarInt64 = [](int64 Value) -> bool
	{
		uint8 Buffer[9];
		const uint32 WriteByteCount = WriteVarInt(Value, Buffer);
		uint32 ReadByteCount = 0;
		const bool bBufferPass = WriteByteCount <= 9 && ReadVarInt(Buffer, ReadByteCount) == Value && ReadByteCount == WriteByteCount;

		uint8 ArBuffer[9];
		FBufferWriter WriteAr(ArBuffer, 9);
		SerializeVarInt(WriteAr, Value);
		FBufferReader ReadAr(ArBuffer, 9, /*bFreeOnClose*/ false);
		int64 ReadValue;
		SerializeVarInt(ReadAr, ReadValue);
		const bool bArchivePass = WriteAr.Tell() == WriteByteCount && ReadValue == Value && ReadAr.Tell() == ReadByteCount;

		return bBufferPass && bArchivePass;
	};
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'0000'0000)"), TestSerializeVarInt64(int64(0x0000'0000'0000'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'0000'0001)"), TestSerializeVarInt64(int64(0x0000'0000'0000'0001)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'0000'003f)"), TestSerializeVarInt64(int64(0x0000'0000'0000'003f)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'0000'0040)"), TestSerializeVarInt64(int64(0x0000'0000'0000'0040)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'0000'1fff)"), TestSerializeVarInt64(int64(0x0000'0000'0000'1fff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'0000'2000)"), TestSerializeVarInt64(int64(0x0000'0000'0000'2000)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'000f'ffff)"), TestSerializeVarInt64(int64(0x0000'0000'000f'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'0010'0000)"), TestSerializeVarInt64(int64(0x0000'0000'0010'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'07ff'ffff)"), TestSerializeVarInt64(int64(0x0000'0000'07ff'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0000'0800'0000)"), TestSerializeVarInt64(int64(0x0000'0000'0800'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0003'ffff'ffff)"), TestSerializeVarInt64(int64(0x0000'0003'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0004'0000'0000)"), TestSerializeVarInt64(int64(0x0000'0004'0000'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'01ff'ffff'ffff)"), TestSerializeVarInt64(int64(0x0000'01ff'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'0200'0000'0000)"), TestSerializeVarInt64(int64(0x0000'0200'0000'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x0000'ffff'ffff'ffff)"), TestSerializeVarInt64(int64(0x0000'ffff'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0001'0000'0000'0000)"), TestSerializeVarInt64(int64(0x0001'0000'0000'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x007f'ffff'ffff'ffff)"), TestSerializeVarInt64(int64(0x007f'ffff'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0x0080'0000'0000'0000)"), TestSerializeVarInt64(int64(0x0080'0000'0000'0000)));
	TestTrue(TEXT("Read/WriteVarInt(0x7fff'ffff'ffff'ffff)"), TestSerializeVarInt64(int64(0x7fff'ffff'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff'ffff'ffff)"), TestSerializeVarInt64(int64(0xffff'ffff'ffff'ffff))); // -0x0000'0000'0000'0001
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff'ffff'ffc0)"), TestSerializeVarInt64(int64(0xffff'ffff'ffff'ffc0))); // -0x0000'0000'0000'0040
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff'ffff'ffbf)"), TestSerializeVarInt64(int64(0xffff'ffff'ffff'ffbf))); // -0x0000'0000'0000'0041
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff'ffff'e000)"), TestSerializeVarInt64(int64(0xffff'ffff'ffff'e000))); // -0x0000'0000'0000'2000
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff'ffff'dfff)"), TestSerializeVarInt64(int64(0xffff'ffff'ffff'dfff))); // -0x0000'0000'0000'2001
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff'fff0'0000)"), TestSerializeVarInt64(int64(0xffff'ffff'fff0'0000))); // -0x0000'0000'0010'0000
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff'ffef'ffff)"), TestSerializeVarInt64(int64(0xffff'ffff'ffef'ffff))); // -0x0000'0000'0010'0001
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff'f800'0000)"), TestSerializeVarInt64(int64(0xffff'ffff'f800'0000))); // -0x0000'0000'0800'0000
	TestTrue(TEXT("Read/WriteVarInt(0xffff'ffff'f7ff'ffff)"), TestSerializeVarInt64(int64(0xffff'ffff'f7ff'ffff))); // -0x0000'0000'0800'0001
	TestTrue(TEXT("Read/WriteVarInt(0xffff'fffc'0000'0000)"), TestSerializeVarInt64(int64(0xffff'fffc'0000'0000))); // -0x0000'0004'0000'0000
	TestTrue(TEXT("Read/WriteVarInt(0xffff'fffb'ffff'ffff)"), TestSerializeVarInt64(int64(0xffff'fffb'ffff'ffff))); // -0x0000'0004'0000'0001
	TestTrue(TEXT("Read/WriteVarInt(0xffff'fe00'0000'0000)"), TestSerializeVarInt64(int64(0xffff'fe00'0000'0000))); // -0x0000'0200'0000'0000
	TestTrue(TEXT("Read/WriteVarInt(0xffff'fdff'ffff'ffff)"), TestSerializeVarInt64(int64(0xffff'fdff'ffff'ffff))); // -0x0000'0200'0000'0001
	TestTrue(TEXT("Read/WriteVarInt(0xffff'0000'0000'0000)"), TestSerializeVarInt64(int64(0xffff'0000'0000'0000))); // -0x0001'0000'0000'0000
	TestTrue(TEXT("Read/WriteVarInt(0xfffe'ffff'ffff'ffff)"), TestSerializeVarInt64(int64(0xfffe'ffff'ffff'ffff))); // -0x0001'0000'0000'0001
	TestTrue(TEXT("Read/WriteVarInt(0xff80'0000'0000'0000)"), TestSerializeVarInt64(int64(0xff80'0000'0000'0000))); // -0x0080'0000'0000'0000
	TestTrue(TEXT("Read/WriteVarInt(0xff7f'ffff'ffff'ffff)"), TestSerializeVarInt64(int64(0xff7f'ffff'ffff'ffff))); // -0x0080'0000'0000'0001
	TestTrue(TEXT("Read/WriteVarInt(0x8000'0000'0000'0000)"), TestSerializeVarInt64(int64(0x8000'0000'0000'0000))); // -0x8000'0000'0000'0000

	// Test Read/WriteVarUInt at unsigned 64-bit encoding boundaries.
	auto TestSerializeVarUInt64 = [](uint64 Value) -> bool
	{
		uint8 Buffer[9];
		const uint32 WriteByteCount = WriteVarUInt(Value, Buffer);
		uint32 ReadByteCount = 0;
		const bool bBufferPass = WriteByteCount <= 9 && ReadVarUInt(Buffer, ReadByteCount) == Value && ReadByteCount == WriteByteCount;

		uint8 ArBuffer[9];
		FBufferWriter WriteAr(ArBuffer, 9);
		SerializeVarUInt(WriteAr, Value);
		FBufferReader ReadAr(ArBuffer, 9, /*bFreeOnClose*/ false);
		uint64 ReadValue;
		SerializeVarUInt(ReadAr, ReadValue);
		const bool bArchivePass = WriteAr.Tell() == WriteByteCount && ReadValue == Value && ReadAr.Tell() == ReadByteCount;

		return bBufferPass && bArchivePass;
	};
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0000'0000)"), TestSerializeVarUInt64(uint64(0x0000'0000'0000'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0000'007f)"), TestSerializeVarUInt64(uint64(0x0000'0000'0000'007f)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0000'0080)"), TestSerializeVarUInt64(uint64(0x0000'0000'0000'0080)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0000'3fff)"), TestSerializeVarUInt64(uint64(0x0000'0000'0000'3fff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0000'4000)"), TestSerializeVarUInt64(uint64(0x0000'0000'0000'4000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0000'7fff)"), TestSerializeVarUInt64(uint64(0x0000'0000'0000'7fff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0000'8000)"), TestSerializeVarUInt64(uint64(0x0000'0000'0000'8000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0000'ffff)"), TestSerializeVarUInt64(uint64(0x0000'0000'0000'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'001f'ffff)"), TestSerializeVarUInt64(uint64(0x0000'0000'001f'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0020'0000)"), TestSerializeVarUInt64(uint64(0x0000'0000'0020'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'0fff'ffff)"), TestSerializeVarUInt64(uint64(0x0000'0000'0fff'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'1000'0000)"), TestSerializeVarUInt64(uint64(0x0000'0000'1000'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'7fff'ffff)"), TestSerializeVarUInt64(uint64(0x0000'0000'7fff'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'8000'0000)"), TestSerializeVarUInt64(uint64(0x0000'0000'8000'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0000'ffff'ffff)"), TestSerializeVarUInt64(uint64(0x0000'0000'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0007'ffff'ffff)"), TestSerializeVarUInt64(uint64(0x0000'0007'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0008'0000'0000)"), TestSerializeVarUInt64(uint64(0x0000'0008'0000'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'03ff'ffff'ffff)"), TestSerializeVarUInt64(uint64(0x0000'03ff'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0000'0400'0000'0000)"), TestSerializeVarUInt64(uint64(0x0000'0400'0000'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0001'ffff'ffff'ffff)"), TestSerializeVarUInt64(uint64(0x0001'ffff'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0002'0000'0000'0000)"), TestSerializeVarUInt64(uint64(0x0002'0000'0000'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0x00ff'ffff'ffff'ffff)"), TestSerializeVarUInt64(uint64(0x00ff'ffff'ffff'ffff)));
	TestTrue(TEXT("Read/WriteVarUInt(0x0100'0000'0000'0000)"), TestSerializeVarUInt64(uint64(0x0100'0000'0000'0000)));
	TestTrue(TEXT("Read/WriteVarUInt(0xffff'ffff'ffff'ffff)"), TestSerializeVarUInt64(uint64(0xffff'ffff'ffff'ffff)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
