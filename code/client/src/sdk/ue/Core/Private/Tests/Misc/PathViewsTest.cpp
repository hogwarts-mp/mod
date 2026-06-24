// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/PathViews.h"

#include "Containers/StringView.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

class FPathViewsTest : public FAutomationTestBase
{
protected:
	using FAutomationTestBase::FAutomationTestBase;

	void TestViewTransform(FStringView (*InFunction)(const FStringView& InPath), const FStringView& InPath, const TCHAR* InExpected)
	{
		const FStringView Actual = InFunction(InPath);
		if (Actual != InExpected)
		{
			AddError(FString::Printf(TEXT("Failed on path '%.*s' (got '%.*s', expected '%s')."),
				InPath.Len(), InPath.GetData(), Actual.Len(), Actual.GetData(), InExpected));
		}
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPathViewsGetCleanFilenameTest, FPathViewsTest, "System.Core.Misc.PathViews.GetCleanFilename", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FPathViewsGetCleanFilenameTest::RunTest(const FString& InParameters)
{
	auto RunGetCleanFilenameTest = [this](const TCHAR* InPath, const TCHAR* InExpected)
	{
		TestViewTransform(FPathViews::GetCleanFilename, InPath, InExpected);
	};

	RunGetCleanFilenameTest(TEXT(""), TEXT(""));
	RunGetCleanFilenameTest(TEXT(".txt"), TEXT(".txt"));
	RunGetCleanFilenameTest(TEXT(".tar.gz"), TEXT(".tar.gz"));
	RunGetCleanFilenameTest(TEXT(".tar.gz/"), TEXT(""));
	RunGetCleanFilenameTest(TEXT(".tar.gz\\"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("File.tar.gz"), TEXT("File.tar.gz"));
	RunGetCleanFilenameTest(TEXT("File.tar.gz/"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("File.tar.gz\\"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("C:/Folder/"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("C:/Folder/File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("C:/Folder/File.tar.gz"), TEXT("File.tar.gz"));
	RunGetCleanFilenameTest(TEXT("C:/Folder/First.Last/File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("File.tar.gz"));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\First.Last\\"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("File.tar.gz"));

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPathViewsGetBaseFilenameTest, FPathViewsTest, "System.Core.Misc.PathViews.GetBaseFilename", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FPathViewsGetBaseFilenameTest::RunTest(const FString& InParameters)
{
	auto RunGetBaseFilenameTest = [this](const TCHAR* InPath, const TCHAR* InExpected, const TCHAR* InExpectedWithPath)
	{
		const FStringView Path = InPath;
		TestViewTransform(FPathViews::GetBaseFilename, Path, InExpected);
		TestViewTransform(FPathViews::GetBaseFilenameWithPath, Path, InExpectedWithPath);
	};

	RunGetBaseFilenameTest(TEXT(""), TEXT(""), TEXT(""));
	RunGetBaseFilenameTest(TEXT(".txt"), TEXT(""), TEXT(""));
	RunGetBaseFilenameTest(TEXT(".tar.gz"), TEXT(".tar"), TEXT(".tar"));
	RunGetBaseFilenameTest(TEXT(".tar.gz/"), TEXT(""), TEXT(".tar.gz/"));
	RunGetBaseFilenameTest(TEXT(".tar.gz\\"), TEXT(""), TEXT(".tar.gz\\"));
	RunGetBaseFilenameTest(TEXT("File"), TEXT("File"), TEXT("File"));
	RunGetBaseFilenameTest(TEXT("File.txt"), TEXT("File"), TEXT("File"));
	RunGetBaseFilenameTest(TEXT("File.tar.gz"), TEXT("File.tar"), TEXT("File.tar"));
	RunGetBaseFilenameTest(TEXT("File.tar.gz/"), TEXT(""), TEXT("File.tar.gz/"));
	RunGetBaseFilenameTest(TEXT("File.tar.gz\\"), TEXT(""), TEXT("File.tar.gz\\"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/"), TEXT(""), TEXT("C:/Folder/"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/File"), TEXT("File"), TEXT("C:/Folder/File"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/File.tar.gz"), TEXT("File.tar"), TEXT("C:/Folder/File.tar"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/First.Last/File"), TEXT("File"), TEXT("C:/Folder/First.Last/File"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/First.Last/File.txt"), TEXT("File"), TEXT("C:/Folder/First.Last/File"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("File.tar"), TEXT("C:/Folder/First.Last/File.tar"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\"), TEXT(""), TEXT("C:\\Folder\\"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\File"), TEXT("File"), TEXT("C:\\Folder\\File"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\First.Last\\"), TEXT(""), TEXT("C:\\Folder\\First.Last\\"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("File"), TEXT("C:\\Folder\\First.Last\\File"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\First.Last\\File.txt"), TEXT("File"), TEXT("C:\\Folder\\First.Last\\File"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("File.tar"), TEXT("C:\\Folder\\First.Last\\File.tar"));

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPathViewsGetPathTest, FPathViewsTest, "System.Core.Misc.PathViews.GetPath", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FPathViewsGetPathTest::RunTest(const FString& InParameters)
{
	auto RunGetPathTest = [this](const TCHAR* InPath, const TCHAR* InExpected)
	{
		TestViewTransform(FPathViews::GetPath, InPath, InExpected);
	};

	RunGetPathTest(TEXT(""), TEXT(""));
	RunGetPathTest(TEXT(".txt"), TEXT(""));
	RunGetPathTest(TEXT(".tar.gz"), TEXT(""));
	RunGetPathTest(TEXT(".tar.gz/"), TEXT(".tar.gz"));
	RunGetPathTest(TEXT(".tar.gz\\"), TEXT(".tar.gz"));
	RunGetPathTest(TEXT("File"), TEXT(""));
	RunGetPathTest(TEXT("File.txt"), TEXT(""));
	RunGetPathTest(TEXT("File.tar.gz"), TEXT(""));
	RunGetPathTest(TEXT("File.tar.gz/"), TEXT("File.tar.gz"));
	RunGetPathTest(TEXT("File.tar.gz\\"), TEXT("File.tar.gz"));
	RunGetPathTest(TEXT("C:/Folder/"), TEXT("C:/Folder"));
	RunGetPathTest(TEXT("C:/Folder/File"), TEXT("C:/Folder"));
	RunGetPathTest(TEXT("C:/Folder/File.tar.gz"), TEXT("C:/Folder"));
	RunGetPathTest(TEXT("C:/Folder/First.Last/File"), TEXT("C:/Folder/First.Last"));
	RunGetPathTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("C:/Folder/First.Last"));
	RunGetPathTest(TEXT("C:\\Folder\\"), TEXT("C:\\Folder"));
	RunGetPathTest(TEXT("C:\\Folder\\File"), TEXT("C:\\Folder"));
	RunGetPathTest(TEXT("C:\\Folder\\First.Last\\"), TEXT("C:\\Folder\\First.Last"));
	RunGetPathTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("C:\\Folder\\First.Last"));
	RunGetPathTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("C:\\Folder\\First.Last"));

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPathViewsGetExtensionTest, FPathViewsTest, "System.Core.Misc.PathViews.GetExtension", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FPathViewsGetExtensionTest::RunTest(const FString& InParameters)
{
	auto RunGetExtensionTest = [this](const TCHAR* InPath, const TCHAR* InExpectedExt, const TCHAR* InExpectedExtDot)
	{
		const FStringView Path = InPath;
		TestViewTransform([](const FStringView& InPath2) { return FPathViews::GetExtension(InPath2, /*bIncludeDot*/ false); }, Path, InExpectedExt);
		TestViewTransform([](const FStringView& InPath2) { return FPathViews::GetExtension(InPath2, /*bIncludeDot*/ true); }, Path, InExpectedExtDot);
	};

	RunGetExtensionTest(TEXT(""), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT(".txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT(".tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT(".tar.gz/"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT(".tar.gz\\"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("File.tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT("File.tar.gz/"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("File.tar.gz\\"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:/Folder/File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:\\Folder\\File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:/Folder/File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("C:\\Folder\\File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("C:/Folder/File.tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT("C:\\Folder\\File.tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT("C:/Folder/First.Last/File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:/Folder/First.Last/File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("C:\\Folder\\First.Last\\File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("gz"), TEXT(".gz"));

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPathViewsGetPathLeafTest, FPathViewsTest, "System.Core.Misc.PathViews.GetPathLeaf", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FPathViewsGetPathLeafTest::RunTest(const FString& InParameters)
{
	auto RunGetPathLeafTest = [this](const TCHAR* InPath, const TCHAR* InExpected)
	{
		TestViewTransform(FPathViews::GetPathLeaf, InPath, InExpected);
	};

	RunGetPathLeafTest(TEXT(""), TEXT(""));
	RunGetPathLeafTest(TEXT(".txt"), TEXT(".txt"));
	RunGetPathLeafTest(TEXT(".tar.gz"), TEXT(".tar.gz"));
	RunGetPathLeafTest(TEXT(".tar.gz/"), TEXT(".tar.gz"));
	RunGetPathLeafTest(TEXT(".tar.gz\\"), TEXT(".tar.gz"));
	RunGetPathLeafTest(TEXT("File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("File.txt"), TEXT("File.txt"));
	RunGetPathLeafTest(TEXT("File.tar.gz"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("File.tar.gz/"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("File.tar.gz\\"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("C:/Folder/"), TEXT("Folder"));
	RunGetPathLeafTest(TEXT("C:/Folder/File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("C:/Folder/File.tar.gz"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("C:/Folder/First.Last/File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\"), TEXT("Folder"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\First.Last\\"), TEXT("First.Last"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("File.tar.gz"));

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPathViewsSplitTest, FPathViewsTest, "System.Core.Misc.PathViews.Split", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FPathViewsSplitTest::RunTest(const FString& InParameters)
{
	auto RunSplitTest = [this](const TCHAR* InPath, const TCHAR* InExpectedPath, const TCHAR* InExpectedName, const TCHAR* InExpectedExt)
	{
		FStringView SplitPath, SplitName, SplitExt;
		FPathViews::Split(InPath, SplitPath, SplitName, SplitExt);
		if (SplitPath != InExpectedPath || SplitName != InExpectedName || SplitExt != InExpectedExt)
		{
			AddError(FString::Printf(TEXT("Failed to split path '%s' (got ('%.*s', '%.*s', '%.*s'), expected ('%s', '%s', '%s'))."), InPath,
				SplitPath.Len(), SplitPath.GetData(), SplitName.Len(), SplitName.GetData(), SplitExt.Len(), SplitExt.GetData(),
				InExpectedPath, InExpectedName, InExpectedExt));
		}
	};

	RunSplitTest(TEXT(""), TEXT(""), TEXT(""), TEXT(""));
	RunSplitTest(TEXT(".txt"), TEXT(""), TEXT(""), TEXT("txt"));
	RunSplitTest(TEXT(".tar.gz"), TEXT(""), TEXT(".tar"), TEXT("gz"));
	RunSplitTest(TEXT(".tar.gz/"), TEXT(".tar.gz"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT(".tar.gz\\"), TEXT(".tar.gz"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("File"), TEXT(""), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("File.txt"), TEXT(""), TEXT("File"), TEXT("txt"));
	RunSplitTest(TEXT("File.tar.gz"), TEXT(""), TEXT("File.tar"), TEXT("gz"));
	RunSplitTest(TEXT("File.tar.gz/"), TEXT("File.tar.gz"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("File.tar.gz\\"), TEXT("File.tar.gz"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("C:/Folder/"), TEXT("C:/Folder"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("C:/Folder/File"), TEXT("C:/Folder"), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("C:/Folder/File.txt"), TEXT("C:/Folder"), TEXT("File"), TEXT("txt"));
	RunSplitTest(TEXT("C:/Folder/File.tar.gz"), TEXT("C:/Folder"), TEXT("File.tar"), TEXT("gz"));
	RunSplitTest(TEXT("C:/Folder/First.Last/File"), TEXT("C:/Folder/First.Last"), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("C:/Folder/First.Last/File.txt"), TEXT("C:/Folder/First.Last"), TEXT("File"), TEXT("txt"));
	RunSplitTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("C:/Folder/First.Last"), TEXT("File.tar"), TEXT("gz"));
	RunSplitTest(TEXT("C:\\Folder\\"), TEXT("C:\\Folder"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("C:\\Folder\\File"), TEXT("C:\\Folder"), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("C:\\Folder\\First.Last\\"), TEXT("C:\\Folder\\First.Last"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("C:\\Folder\\First.Last"), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("C:\\Folder\\First.Last\\File.txt"), TEXT("C:\\Folder\\First.Last"), TEXT("File"), TEXT("txt"));
	RunSplitTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("C:\\Folder\\First.Last"), TEXT("File.tar"), TEXT("gz"));

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPathViewsChangeExtensionTest, FPathViewsTest, "System.Core.Misc.PathViews.ChangeExtension", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FPathViewsChangeExtensionTest::RunTest(const FString& InParameters)
{
	auto RunChangeExtensionTest = [this](const TCHAR* InPath, const TCHAR* InNewExt, const TCHAR* InExpectedPath)
	{
		// Run test
		const FString NewPath = FPathViews::ChangeExtension(InPath, InNewExt);
		if (NewPath != InExpectedPath)
		{
			AddError(FString::Printf(TEXT("Path '%s' failed to change the extension (got '%s', expected '%s')."), InPath, *NewPath, InExpectedPath));
		}
	};

	RunChangeExtensionTest(nullptr, nullptr, TEXT(""));
	RunChangeExtensionTest(TEXT(""), TEXT(""), TEXT(""));
	RunChangeExtensionTest(TEXT(""), TEXT(".txt"), TEXT(""));
	RunChangeExtensionTest(TEXT("file"), TEXT("log"), TEXT("file"));
	RunChangeExtensionTest(TEXT("file.txt"), TEXT("log"), TEXT("file.log"));
	RunChangeExtensionTest(TEXT("file.tar.gz"), TEXT("gz2"), TEXT("file.tar.gz2"));
	RunChangeExtensionTest(TEXT("file.txt"), TEXT(""), TEXT("file"));
	RunChangeExtensionTest(TEXT("C:/Folder/file"), TEXT("log"), TEXT("C:/Folder/file"));
	RunChangeExtensionTest(TEXT("C:/Folder/file.txt"), TEXT("log"), TEXT("C:/Folder/file.log"));
	RunChangeExtensionTest(TEXT("C:/Folder/file.tar.gz"), TEXT("gz2"), TEXT("C:/Folder/file.tar.gz2"));
	RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file"), TEXT("log"), TEXT("C:/Folder/First.Last/file"));
	RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file.txt"), TEXT("log"), TEXT("C:/Folder/First.Last/file.log"));
	RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file.tar.gz"), TEXT("gz2"), TEXT("C:/Folder/First.Last/file.tar.gz2"));

	return true;
}

#endif
