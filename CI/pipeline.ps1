param(
    [string]$github_workspace,
    [string]$revision
)

Write-Output "Workspace is $github_workspace"
Write-Output "Github revision is $revision"

$slRevision = 0

try {
	# Download data for revisions
	$urlJsonObsVersions = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"

	$filepathJsonPublish = ".\meta_publish.json"
	Invoke-WebRequest -Uri $urlJsonObsVersions -OutFile $filepathJsonPublish
	$jsonContent = Get-Content -Path $filepathJsonPublish -Raw | ConvertFrom-Json
	
	$slRevision = $jsonContent.next_rev
	Write-Output "Streamlabs revision is $slRevision"
}
catch {
	throw "Error: An error occurred. Details: $($_.Exception.Message)"
}

$env:Protobuf_DIR = "${github_workspace}\..\grpc_dist\cmake"
$env:absl_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\absl"
$env:gRPC_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\grpc"
$env:utf8_range_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\utf8_range"

# Edit the CMAKE with the SL_OBS_VERSION and $revision
# Read the content of CMakeLists.txt into a variable
$cmakeContent = Get-Content -Path .\CMakeLists.txt -Raw

# Replace the placeholders with the actual environment variable value
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser PRIVATE SL_OBS_VERSION=""\)', "target_compile_definitions(sl-browser PRIVATE SL_OBS_VERSION=`"$($env:SL_OBS_VERSION)`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser-plugin PRIVATE SL_OBS_VERSION=""\)', "target_compile_definitions(sl-browser-plugin PRIVATE SL_OBS_VERSION=`"$($env:SL_OBS_VERSION)`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser PRIVATE GITHUB_REVISION=""\)', "target_compile_definitions(sl-browser PRIVATE GITHUB_REVISION=`"${revision}`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser-plugin PRIVATE GITHUB_REVISION=""\)', "target_compile_definitions(sl-browser-plugin PRIVATE GITHUB_REVISION=`"${revision}`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser PRIVATE SL_REVISION=""\)', "target_compile_definitions(sl-browser PRIVATE SL_REVISION=`"${revision}`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser-plugin PRIVATE SL_REVISION=""\)', "target_compile_definitions(sl-browser-plugin PRIVATE SL_REVISION=`"${slRevision}`")"

# Write the updated content back to CMakeLists.txt
Set-Content -Path .\CMakeLists.txt -Value $cmakeContent

# Output the updated content to the console
Write-Output "Updated cmake with SL_OBS_VERSION definition:"
Write-Output $cmakeContent

# We start inside obs-sl-browser folder, move up to make room for cloning OBS and moving obs-sl-browser into it
cd ..\

# Deps
.\obs-sl-browser\ci\install_deps.cmd

# Read the obs.ver file to get the branch name
$branchName = Get-Content -Path ".\obs-sl-browser\obs.ver" -Raw

# Clone obs-studio repository with the branch name
git clone --recursive --branch $branchName https://github.com/obsproject/obs-studio.git

# Rename 'obs-studio' folder to name of git tree so pdb's know which version it was built with
Rename-Item -Path ".\obs-studio" -NewName $revision

# Update submodules in obs-studio
cd $revision
git submodule update --init --recursive

# Add new line to CMakeLists.txt in obs-studio\plugins
$cmakeListsPath = ".\plugins\CMakeLists.txt"
$addSubdirectoryLine = "add_subdirectory(obs-sl-browser)"
Add-Content -Path $cmakeListsPath -Value $addSubdirectoryLine

# Move obs-sl-browser folder into obs-studio\plugins
Copy-Item -Path "..\obs-sl-browser" -Destination ".\plugins\obs-sl-browser" -Recurse

# Build
try {
    .\CI\build-windows.ps1 -ErrorAction Stop
}
catch {
    # Handle the error
    Write-Host "Error: $_"
    exit 1
}
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code ${LastExitCode}"
}
	
# Copy platforms folder to plugin release fodler
Copy-Item -Path ".\build64\rundir\RelWithDebInfo\bin\64bit\platforms" -Destination ".\build64\plugins\obs-sl-browser\RelWithDebInfo" -Recurse

# Clone symbols store scripts
Write-Output "-- Symbols"
cd ..\
git clone --recursive --branch "no-http-source" https://github.com/stream-labs/symsrv-scripts.git

# Run symbols (re-try for 5 minutes)
cd symsrv-scripts
$startTime = Get-Date
$maxDuration = New-TimeSpan -Minutes 5

# Last error code initialized to a non-zero value
$lastExitCode = 1

while ((Get-Date) - $startTime -lt $maxDuration -and $lastExitCode -ne 0) {
    .\main.ps1 -localSourceDir "${github_workspace}\..\${revision}\build64\plugins\obs-sl-browser\RelWithDebInfo"

    # Update the last exit code
    $lastExitCode = $LastExitCode

    # Add a delay between retries
    Start-Sleep -Seconds 10
}

# Check if the last exit code is non-zero and throw the error
if ($lastExitCode -ne 0) {
    throw "Symbol processing script exited with error code $lastExitCode"
}


# Define the output file name for the 7z archive
Write-Output "-- 7z"
$pathToArchive = "${github_workspace}\..\${revision}\build64\plugins\obs-sl-browser\RelWithDebInfo"
Write-Output $pathToArchive

# Check if the path exists
if (Test-Path -Path $pathToArchive) {
    # Create a 7z archive of the $revision folder
    $archiveFileName = "slplugin-$env:SL_OBS_VERSION-$revision.7z"
    7z a $archiveFileName $pathToArchive

    # Output the name of the archive file created
    Write-Output "Archive created: $archiveFileName"
} else {
    # Throw an error if the path does not exist
    throw "Error: The path $pathToArchive does not exist."
}

# Output the name of the archive file created
Write-Output "Archive created: $archiveFileName"

# Move the 7z archive to the $github_workspace directory
Move-Item -Path $archiveFileName -Destination "${github_workspace}\"

# Add information to the revision library
$slRevision = 0

try {
    # Download data for revisions
    $urlJsonObsVersions = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"
    $filepathJsonPublish = ".\meta_publish.json"
    Invoke-WebRequest -Uri $urlJsonObsVersions -OutFile $filepathJsonPublish
    $jsonContent = Get-Content -Path $filepathJsonPublish -Raw | ConvertFrom-Json
    
    $slRevision = $jsonContent.next_rev
    Write-Output "Streamlabs revision is $slRevision"
    
    # Attempt to download existing revision builds JSON
    $urlJsonRevisionBuilds = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/revision_builds.json"
    $filepathJsonRevisionBuilds = ".\revision_builds.json"
    
    try {
        Invoke-WebRequest -Uri $urlJsonRevisionBuilds -OutFile $filepathJsonRevisionBuilds
    }
    catch {
        # If download fails, assume file does not exist and create a new JSON array
        Set-Content -Path $filepathJsonRevisionBuilds -Value "[]"
    }

    # Load or initialize the revision builds JSON
    $revisionBuilds = Get-Content -Path $filepathJsonRevisionBuilds -Raw | ConvertFrom-Json
    
    # Prepare the new entry
    $newEntry = @{
        "rev" = $slRevision
        "date" = (Get-Date -UFormat %s)
        "gitrev" = $revision
    }

    # Check if the entry already exists (based on rev and gitrev)
    $exists = $false
    foreach ($entry in $revisionBuilds) {
        if ($entry.rev -eq $newEntry.rev -and $entry.gitrev -eq $newEntry.gitrev) {
            $exists = $true
            break
        }
    }
    
    # If the entry does not exist, add it
    if (-not $exists) {
        $revisionBuilds += $newEntry
    }
    
    # Save the updated JSON back to the file
    $revisionBuilds | ConvertTo-Json -Depth 100 | Set-Content -Path $filepathJsonRevisionBuilds
    
    Write-Output "Revision builds updated successfully."
}
catch {
    throw "Error: An error occurred. Details: $($_.Exception.Message)"
}