// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Containers/UnrealString.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathTests, "System.Core.Misc.Paths", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FPathTests::RunTest( const FString& Parameters )
{
	// Directory collapsing
	{
		auto RunCollapseRelativeDirectoriesTest = [this](const TCHAR* InPath, const TCHAR* InResult)
		{
			// Run test
			FString CollapsedPath = InPath;
			const bool bValid = FPaths::CollapseRelativeDirectories(CollapsedPath);

			if (InResult)
			{
				// If we're looking for a result, make sure it was returned correctly
				if (!bValid || CollapsedPath != InResult)
				{
					AddError(FString::Printf(TEXT("Path '%s' failed to collapse correctly (got '%s', expected '%s')."), InPath, *CollapsedPath, InResult));
				}
			}
			else
			{
				// Otherwise, make sure it failed
				if (bValid)
				{
					AddError(FString::Printf(TEXT("Path '%s' collapsed unexpectedly."), InPath));
				}
			}
		};

		RunCollapseRelativeDirectoriesTest(TEXT(".."),                                                   NULL);
		RunCollapseRelativeDirectoriesTest(TEXT("/.."),                                                  NULL);
		RunCollapseRelativeDirectoriesTest(TEXT("./"),                                                   TEXT(""));
		RunCollapseRelativeDirectoriesTest(TEXT("./file.txt"),                                           TEXT("file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("/."),                                                   TEXT("/."));
		RunCollapseRelativeDirectoriesTest(TEXT("Folder"),                                               TEXT("Folder"));
		RunCollapseRelativeDirectoriesTest(TEXT("/Folder"),                                              TEXT("/Folder"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder"),                                            TEXT("C:/Folder"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder/.."),                                         TEXT("C:"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder/../"),                                        TEXT("C:/"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder/../file.txt"),                                TEXT("C:/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("Folder/.."),                                            TEXT(""));
		RunCollapseRelativeDirectoriesTest(TEXT("Folder/../"),                                           TEXT("/"));
		RunCollapseRelativeDirectoriesTest(TEXT("Folder/../file.txt"),                                   TEXT("/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("/Folder/.."),                                           TEXT(""));
		RunCollapseRelativeDirectoriesTest(TEXT("/Folder/../"),                                          TEXT("/"));
		RunCollapseRelativeDirectoriesTest(TEXT("/Folder/../file.txt"),                                  TEXT("/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("Folder/../.."),                                         NULL);
		RunCollapseRelativeDirectoriesTest(TEXT("Folder/../../"),                                        NULL);
		RunCollapseRelativeDirectoriesTest(TEXT("Folder/../../file.txt"),                                NULL);
		RunCollapseRelativeDirectoriesTest(TEXT("C:/.."),                                                NULL);
		RunCollapseRelativeDirectoriesTest(TEXT("C:/."),                                                 TEXT("C:/."));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/./"),                                                TEXT("C:/"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/./file.txt"),                                        TEXT("C:/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/../Folder2"),                                TEXT("C:/Folder2"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/../Folder2/"),                               TEXT("C:/Folder2/"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/../Folder2/file.txt"),                       TEXT("C:/Folder2/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/../Folder2/../.."),                          NULL);
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/../Folder2/../Folder3"),                     TEXT("C:/Folder3"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/../Folder2/../Folder3/"),                    TEXT("C:/Folder3/"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/../Folder2/../Folder3/file.txt"),            TEXT("C:/Folder3/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/../../Folder3"),                     TEXT("C:/Folder3"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/../../Folder3/"),                    TEXT("C:/Folder3/"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/../../Folder3/file.txt"),            TEXT("C:/Folder3/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/../../Folder3/../Folder4"),          TEXT("C:/Folder4"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/../../Folder3/../Folder4/"),         TEXT("C:/Folder4/"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/../../Folder3/../Folder4/file.txt"), TEXT("C:/Folder4/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/../Folder3/../../Folder4"),          TEXT("C:/Folder4"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/../Folder3/../../Folder4/"),         TEXT("C:/Folder4/"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/../Folder3/../../Folder4/file.txt"), TEXT("C:/Folder4/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/.././../Folder4"),                   TEXT("C:/Folder4"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/.././../Folder4/"),                  TEXT("C:/Folder4/"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/Folder2/.././../Folder4/file.txt"),          TEXT("C:/Folder4/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/A/B/.././../C"),                                     TEXT("C:/C"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/A/B/.././../C/"),                                    TEXT("C:/C/"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/A/B/.././../C/file.txt"),                            TEXT("C:/C/file.txt"));
		RunCollapseRelativeDirectoriesTest(TEXT(".svn"),                                                 TEXT(".svn"));
		RunCollapseRelativeDirectoriesTest(TEXT("/.svn"),                                                TEXT("/.svn"));
		RunCollapseRelativeDirectoriesTest(TEXT("./Folder/.svn"),                                        TEXT("Folder/.svn"));
		RunCollapseRelativeDirectoriesTest(TEXT("./.svn/../.svn"),                                       TEXT(".svn"));
		RunCollapseRelativeDirectoriesTest(TEXT(".svn/./.svn/.././../.svn"),                             TEXT("/.svn"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/./Folder2/..Folder3"),						 TEXT("C:/Folder1/Folder2/..Folder3"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/./Folder2/..Folder3/Folder4"),				 TEXT("C:/Folder1/Folder2/..Folder3/Folder4"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/./Folder2/..Folder3/..Folder4"),			 TEXT("C:/Folder1/Folder2/..Folder3/..Folder4"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/./Folder2/..Folder3/Folder4/../Folder5"),	 TEXT("C:/Folder1/Folder2/..Folder3/Folder5"));
		RunCollapseRelativeDirectoriesTest(TEXT("C:/Folder1/..Folder2/Folder3/..Folder4/../Folder5"),	 TEXT("C:/Folder1/..Folder2/Folder3/Folder5"));
	}

	// Extension texts
	{
		auto RunGetExtensionTest = [this](const TCHAR* InPath, const TCHAR* InExpectedExt)
		{
			// Run test
			const FString Ext = FPaths::GetExtension(FString(InPath));
			if (Ext != InExpectedExt)
			{
				AddError(FString::Printf(TEXT("Path '%s' failed to get the extension (got '%s', expected '%s')."), InPath, *Ext, InExpectedExt));
			}
		};

		RunGetExtensionTest(TEXT("file"),									TEXT(""));
		RunGetExtensionTest(TEXT("file.txt"),								TEXT("txt"));
		RunGetExtensionTest(TEXT("file.tar.gz"),							TEXT("gz"));
		RunGetExtensionTest(TEXT("C:/Folder/file"),							TEXT(""));
		RunGetExtensionTest(TEXT("C:/Folder/file.txt"),						TEXT("txt"));
		RunGetExtensionTest(TEXT("C:/Folder/file.tar.gz"),					TEXT("gz"));
		RunGetExtensionTest(TEXT("C:/Folder/First.Last/file"),				TEXT(""));
		RunGetExtensionTest(TEXT("C:/Folder/First.Last/file.txt"),			TEXT("txt"));
		RunGetExtensionTest(TEXT("C:/Folder/First.Last/file.tar.gz"),		TEXT("gz"));

		auto RunSetExtensionTest = [this](const TCHAR* InPath, const TCHAR* InNewExt, const FString& InExpectedPath)
		{
			// Run test
			const FString NewPath = FPaths::SetExtension(FString(InPath), FString(InNewExt));
			if (NewPath != InExpectedPath)
			{
				AddError(FString::Printf(TEXT("Path '%s' failed to set the extension (got '%s', expected '%s')."), InPath, *NewPath, *InExpectedPath));
			}
		};

		RunSetExtensionTest(TEXT("file"),									TEXT("log"),	TEXT("file.log"));
		RunSetExtensionTest(TEXT("file.txt"),								TEXT("log"),	TEXT("file.log"));
		RunSetExtensionTest(TEXT("file.tar.gz"),							TEXT("gz2"),	TEXT("file.tar.gz2"));
		RunSetExtensionTest(TEXT("C:/Folder/file"),							TEXT("log"),	TEXT("C:/Folder/file.log"));
		RunSetExtensionTest(TEXT("C:/Folder/file.txt"),						TEXT("log"),	TEXT("C:/Folder/file.log"));
		RunSetExtensionTest(TEXT("C:/Folder/file.tar.gz"),					TEXT("gz2"),	TEXT("C:/Folder/file.tar.gz2"));
		RunSetExtensionTest(TEXT("C:/Folder/First.Last/file"),				TEXT("log"),	TEXT("C:/Folder/First.Last/file.log"));
		RunSetExtensionTest(TEXT("C:/Folder/First.Last/file.txt"),			TEXT("log"),	TEXT("C:/Folder/First.Last/file.log"));
		RunSetExtensionTest(TEXT("C:/Folder/First.Last/file.tar.gz"),		TEXT("gz2"),	TEXT("C:/Folder/First.Last/file.tar.gz2"));

		auto RunChangeExtensionTest = [this](const TCHAR* InPath, const TCHAR* InNewExt, const FString& InExpectedPath)
		{
			// Run test
			const FString NewPath = FPaths::ChangeExtension(FString(InPath), FString(InNewExt));
			if (NewPath != InExpectedPath)
			{
				AddError(FString::Printf(TEXT("Path '%s' failed to change the extension (got '%s', expected '%s')."), InPath, *NewPath, *InExpectedPath));
			}
		};

		RunChangeExtensionTest(TEXT("file"),								TEXT("log"),	TEXT("file"));
		RunChangeExtensionTest(TEXT("file.txt"),							TEXT("log"),	TEXT("file.log"));
		RunChangeExtensionTest(TEXT("file.tar.gz"),							TEXT("gz2"),	TEXT("file.tar.gz2"));
		RunChangeExtensionTest(TEXT("C:/Folder/file"),						TEXT("log"),	TEXT("C:/Folder/file"));
		RunChangeExtensionTest(TEXT("C:/Folder/file.txt"),					TEXT("log"),	TEXT("C:/Folder/file.log"));
		RunChangeExtensionTest(TEXT("C:/Folder/file.tar.gz"),				TEXT("gz2"),	TEXT("C:/Folder/file.tar.gz2"));
		RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file"),			TEXT("log"),	TEXT("C:/Folder/First.Last/file"));
		RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file.txt"),		TEXT("log"),	TEXT("C:/Folder/First.Last/file.log"));
		RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file.tar.gz"),	TEXT("gz2"),	TEXT("C:/Folder/First.Last/file.tar.gz2"));
	}

	// IsUnderDirectory
	{
		auto RunIsUnderDirectoryTest = [this](const TCHAR* InPath1, const TCHAR* InPath2, bool ExpectedResult)
		{
			// Run test
			bool Result = FPaths::IsUnderDirectory(FString(InPath1), FString(InPath2));
			if (Result != ExpectedResult)
			{
				AddError(FString::Printf(TEXT("FPaths::IsUnderDirectory('%s', '%s') != %s."), InPath1, InPath2, ExpectedResult ? TEXT("true") : TEXT("false")));
			}
		};

		RunIsUnderDirectoryTest(TEXT("C:/Folder"),			TEXT("C:/FolderN"), false);
		RunIsUnderDirectoryTest(TEXT("C:/Folder1"),			TEXT("C:/Folder2"), false);
		RunIsUnderDirectoryTest(TEXT("C:/Folder"),			TEXT("C:/Folder/SubDir"), false);

		RunIsUnderDirectoryTest(TEXT("C:/Folder"),			TEXT("C:/Folder"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/File"),		TEXT("C:/Folder"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/File"),		TEXT("C:/Folder/"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/"),			TEXT("C:/Folder"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/"),			TEXT("C:/Folder/"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/Subdir/"),	TEXT("C:/Folder"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/Subdir/"),	TEXT("C:/Folder/"), true);
	}

	// RemoveDuplicateSlashes
	{
		auto RunRemoveDuplicateSlashesTest = [this](const TCHAR* InPath, const TCHAR* InExpectedResult)
		{
			FString ReplacementPath(InPath);
			const FString ExpectedResult(InExpectedResult);
			FPaths::RemoveDuplicateSlashes(ReplacementPath);
			if (!ReplacementPath.Equals(ExpectedResult, ESearchCase::CaseSensitive))
			{
				AddError(FString::Printf(TEXT("FPaths::RemoveDuplicateSlashes('%s') != '%s'."), InPath, InExpectedResult));
			}
			else if (ReplacementPath.GetCharArray().Num() != 0 && ReplacementPath.GetCharArray().Num() != ExpectedResult.Len() + 1)
			{
				AddError(FString::Printf(TEXT("FPaths::RemoveDuplicateSlashes('%s') returned a result with extra space still allocated after the null terminator."), InPath));
			}
		};

		RunRemoveDuplicateSlashesTest(TEXT(""), TEXT(""));
		RunRemoveDuplicateSlashesTest(TEXT("C:/Folder/File.txt"), TEXT("C:/Folder/File.txt"));
		RunRemoveDuplicateSlashesTest(TEXT("C:/Folder/File/"), TEXT("C:/Folder/File/"));
		RunRemoveDuplicateSlashesTest(TEXT("/"), TEXT("/"));
		RunRemoveDuplicateSlashesTest(TEXT("//"), TEXT("/"));
		RunRemoveDuplicateSlashesTest(TEXT("////"), TEXT("/"));
		RunRemoveDuplicateSlashesTest(TEXT("/Folder/File"), TEXT("/Folder/File"));
		RunRemoveDuplicateSlashesTest(TEXT("//Folder/File"), TEXT("/Folder/File")); // Don't use on //UNC paths; it will be stripped!
		RunRemoveDuplicateSlashesTest(TEXT("/////Folder//////File/////"), TEXT("/Folder/File/"));
		RunRemoveDuplicateSlashesTest(TEXT("\\\\Folder\\\\File\\\\"), TEXT("\\\\Folder\\\\File\\\\")); // It doesn't strip backslash, and we rely on that in some places
		RunRemoveDuplicateSlashesTest(TEXT("//\\\\//Folder//\\\\//File//\\\\//"), TEXT("/\\\\/Folder/\\\\/File/\\\\/"));
	}

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
