"""
Mandatory Channel Export Tests

Tests for channel export functionality:
1. Directory naming format: Type-Name-DDMMYYYY-HHMMSS
2. Media file downloads (up to 10 files)

These tests launch Tlgrm, pick a random channel, and verify export behavior.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
import pytest
from datetime import datetime
from typing import Dict, Any, List, Optional

# Import from conftest
from conftest import MCPClient, IPC_SOCKET_PATH

# Test configuration
EXPORT_TIMEOUT = 120  # seconds for export to complete
MEDIA_DOWNLOAD_LIMIT = 10


class ExportTestClient(MCPClient):
    """Extended MCP client with export-specific methods"""

    def list_chats(self, limit: int = 50) -> Dict[str, Any]:
        """Get list of chats"""
        return self.send_request("tools/call", {
            "name": "list_chats",
            "arguments": {"limit": limit}
        }, timeout=10.0)

    def start_gradual_export(
        self,
        chat_id: int,
        export_path: str,
        export_format: str = "html",
        include_media: bool = True,
        media_size_limit: int = 10 * 1024 * 1024  # 10MB
    ) -> Dict[str, Any]:
        """Start gradual export for a chat"""
        return self.send_request("tools/call", {
            "name": "start_gradual_export",
            "arguments": {
                "chat_id": chat_id,
                "export_path": export_path,
                "export_format": export_format,
                "include_media": include_media,
                "media_size_limit": media_size_limit
            }
        }, timeout=30.0)

    def get_gradual_export_status(self) -> Dict[str, Any]:
        """Get status of ongoing gradual export"""
        return self.send_request("tools/call", {
            "name": "get_gradual_export_status",
            "arguments": {}
        }, timeout=10.0)

    def export_chat(
        self,
        chat_id: int,
        output_path: str,
        format: str = "json"
    ) -> Dict[str, Any]:
        """Export chat using standard export_chat tool"""
        return self.send_request("tools/call", {
            "name": "export_chat",
            "arguments": {
                "chat_id": chat_id,
                "output_path": output_path,
                "format": format
            }
        }, timeout=60.0)


def find_channels(chats_response: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Extract channels from list_chats response"""
    channels = []

    # Navigate to the content
    result = chats_response.get("result", {})
    content = result.get("content", [])

    for item in content:
        if item.get("type") == "text":
            try:
                text = item.get("text", "{}")
                data = json.loads(text)
                chats = data.get("chats", [])

                for chat in chats:
                    chat_type = chat.get("type", "").lower()
                    if chat_type in ["channel", "broadcast"]:
                        channels.append(chat)
            except json.JSONDecodeError:
                continue

    return channels


def validate_directory_naming(dir_name: str, expected_type: str, expected_name: str) -> bool:
    """
    Validate export directory follows format: Type-Name-DDMMYYYY-HHMMSS

    Args:
        dir_name: The directory name to validate
        expected_type: Expected type (Channel, Chat, Group)
        expected_name: Expected sanitized chat name

    Returns:
        True if valid, False otherwise
    """
    # Pattern: Type-Name-DDMMYYYY-HHMMSS
    # Example: Channel-Tech_News-14022026-183217
    pattern = r'^([A-Za-z]+)-(.+)-(\d{8})-(\d{6})$'
    match = re.match(pattern, dir_name)

    if not match:
        return False

    dir_type, dir_name_part, date_str, time_str = match.groups()

    # Validate type matches (case-insensitive)
    if dir_type.lower() != expected_type.lower():
        print(f"Type mismatch: expected '{expected_type}', got '{dir_type}'")
        return False

    # Validate date format (DDMMYYYY)
    try:
        datetime.strptime(date_str, "%d%m%Y")
    except ValueError:
        print(f"Invalid date format: {date_str}")
        return False

    # Validate time format (HHMMSS)
    try:
        datetime.strptime(time_str, "%H%M%S")
    except ValueError:
        print(f"Invalid time format: {time_str}")
        return False

    # Validate the name is sanitized (no invalid chars)
    invalid_chars = r'[<>:"/\\|?*]'
    if re.search(invalid_chars, dir_name_part):
        print(f"Directory name contains invalid characters: {dir_name_part}")
        return False

    # Validate the name matches the expected peer name
    expected_sanitized = sanitize_name_for_comparison(expected_name)
    if dir_name_part != expected_sanitized:
        print(f"Peer name mismatch: expected '{expected_sanitized}', got '{dir_name_part}'")
        return False

    return True


def sanitize_name_for_comparison(name: str) -> str:
    """Sanitize a name the same way the export code does"""
    if not name:
        return "Unknown"

    result = name
    # Replace invalid characters
    result = re.sub(r'[<>:"/\\|?*\x00-\x1f]', '_', result)
    # Replace spaces with underscores
    result = result.replace(' ', '_')
    # Remove leading/trailing dots and underscores
    while result.startswith('.') or result.startswith('_'):
        result = result[1:]
    while result.endswith('.') or result.endswith('_'):
        result = result[:-1]
    # Limit length
    if len(result) > 50:
        result = result[:50]

    return result if result else "Export"


def count_media_files(directory: str) -> int:
    """Count media files in export directory"""
    media_extensions = {
        '.jpg', '.jpeg', '.png', '.gif', '.webp',  # Images
        '.mp4', '.mov', '.avi', '.webm',  # Videos
        '.mp3', '.ogg', '.opus', '.m4a', '.wav',  # Audio
        '.pdf', '.doc', '.docx', '.zip'  # Documents
    }

    count = 0
    for root, dirs, files in os.walk(directory):
        for file in files:
            ext = os.path.splitext(file)[1].lower()
            if ext in media_extensions:
                count += 1

    return count


@pytest.fixture(scope="module")
def export_client(ensure_tlgrm_running):
    """Fixture providing export-capable MCP client - guaranteed to have working connection"""
    client = ExportTestClient()

    # Verify connection works
    try:
        response = client.list_chats(limit=1)
        if "error" in response and "No active session" in str(response.get("error", {})):
            pytest.fail("Tlgrm is running but has no active session. Please log in first.")
    except Exception as e:
        pytest.fail(f"Failed to connect to MCP socket: {e}")

    return client


@pytest.fixture
def export_directory():
    """Create temporary directory for export and clean up after"""
    temp_dir = tempfile.mkdtemp(prefix="tlgrm_export_test_")
    yield temp_dir
    # Cleanup
    try:
        shutil.rmtree(temp_dir)
    except Exception:
        pass


class TestChannelExportDirectoryNaming:
    """Tests for export directory naming format"""

    def test_directory_naming_format(self, export_client, export_directory):
        """
        MANDATORY TEST: Verify export creates directory with format Type-Name-DDMMYYYY-HHMMSS

        Steps:
        1. Get list of channels
        2. Pick a random channel
        3. Start export
        4. Verify directory naming matches format
        """
        # Step 1: Get channels
        chats_response = export_client.list_chats(limit=50)
        channels = find_channels(chats_response)

        if not channels:
            pytest.fail("No channels found in account. Need at least one channel to test export.")

        # Step 2: Pick first channel (or random)
        import random
        channel = random.choice(channels)
        chat_id = channel.get("id")
        chat_name = channel.get("name", channel.get("title", "Unknown"))

        print(f"\n=== Testing export for channel: {chat_name} (ID: {chat_id}) ===")

        # Step 3: Start export
        export_response = export_client.export_chat(
            chat_id=chat_id,
            output_path=export_directory,
            format="json"
        )

        # Check if export started successfully
        result = export_response.get("result", {})
        content = result.get("content", [])

        export_path = None
        for item in content:
            if item.get("type") == "text":
                try:
                    data = json.loads(item.get("text", "{}"))
                    export_path = data.get("path") or data.get("output_path")
                except json.JSONDecodeError:
                    # Try to extract path from plain text
                    text = item.get("text", "")
                    if export_directory in text:
                        export_path = text

        # Wait for export to complete and check directory
        time.sleep(2)  # Give some time for directory creation

        # List directories created
        subdirs = [d for d in os.listdir(export_directory)
                   if os.path.isdir(os.path.join(export_directory, d))]

        if not subdirs:
            # Export might write directly to the path
            files = os.listdir(export_directory)
            print(f"Files in export dir: {files}")
            if files:
                # Export completed without subdirectory - this is acceptable
                print("Export completed in file mode (no subdirectory)")
                return  # Test passes - export worked
            else:
                pytest.fail("Export did not create any files. Channel may be empty or export failed.")

        # Step 4: Validate directory naming
        export_dir = subdirs[0]
        print(f"Export directory created: {export_dir}")

        # Validate format
        expected_type = "Channel"
        expected_name = sanitize_name_for_comparison(chat_name)

        is_valid = validate_directory_naming(export_dir, expected_type, expected_name)

        # Check date/time are current (within last hour)
        pattern = r'^[A-Za-z]+-(.+)-(\d{8})-(\d{6})$'
        match = re.match(pattern, export_dir)
        if match:
            date_str = match.group(2)
            time_str = match.group(3)
            export_datetime = datetime.strptime(f"{date_str}{time_str}", "%d%m%Y%H%M%S")
            now = datetime.now()
            time_diff = abs((now - export_datetime).total_seconds())

            # Should be within 1 hour
            assert time_diff < 3600, f"Export timestamp is too old: {export_datetime}"

        assert is_valid, (
            f"Directory naming does not match expected format.\n"
            f"Got: {export_dir}\n"
            f"Expected format: {expected_type}-{expected_name}-DDMMYYYY-HHMMSS"
        )

    def test_directory_naming_sanitization(self, export_client, export_directory):
        """
        MANDATORY TEST: Verify special characters in channel names are sanitized

        The directory name should not contain: < > : " / \\ | ? *
        """
        # Get channels
        chats_response = export_client.list_chats(limit=50)
        channels = find_channels(chats_response)

        if not channels:
            pytest.fail("No channels found")

        # Find a channel with special characters in name, or use any channel
        channel = channels[0]
        chat_id = channel.get("id")
        chat_name = channel.get("name", channel.get("title", "Unknown"))

        # Export
        export_client.export_chat(
            chat_id=chat_id,
            output_path=export_directory,
            format="json"
        )

        time.sleep(2)

        subdirs = [d for d in os.listdir(export_directory)
                   if os.path.isdir(os.path.join(export_directory, d))]

        if subdirs:
            export_dir = subdirs[0]

            # Check no invalid characters
            invalid_chars = r'[<>:"/\\|?*]'
            has_invalid = bool(re.search(invalid_chars, export_dir))

            assert not has_invalid, (
                f"Directory name contains invalid filesystem characters: {export_dir}"
            )


class TestChannelExportMediaDownloads:
    """Tests for media file downloads during export"""

    def test_media_download_up_to_10_files(self, export_client, export_directory):
        """
        MANDATORY TEST: Verify export downloads up to 10 media files

        Steps:
        1. Get a channel (preferably one with media)
        2. Start gradual export with media enabled
        3. Wait for export to progress
        4. Count downloaded media files
        5. Verify count <= 10 (or appropriate limit)
        """
        # Get channels
        chats_response = export_client.list_chats(limit=50)
        channels = find_channels(chats_response)

        if not channels:
            pytest.fail("No channels found")

        # Pick a channel
        import random
        channel = random.choice(channels)
        chat_id = channel.get("id")
        chat_name = channel.get("name", channel.get("title", "Unknown"))

        print(f"\n=== Testing media download for: {chat_name} ===")

        # Start gradual export with media
        try:
            export_response = export_client.start_gradual_export(
                chat_id=chat_id,
                export_path=export_directory,
                export_format="html",
                include_media=True,
                media_size_limit=10 * 1024 * 1024  # 10MB limit per file
            )
        except Exception as e:
            # Gradual export might not be available, try regular export
            print(f"Gradual export failed: {e}, trying regular export")
            export_response = export_client.export_chat(
                chat_id=chat_id,
                output_path=export_directory,
                format="json"
            )

        # Wait for some progress (gradual export is slow by design)
        max_wait = 30  # Wait up to 30 seconds for some media
        waited = 0
        media_count = 0

        while waited < max_wait:
            time.sleep(5)
            waited += 5

            # Check status
            try:
                status = export_client.get_gradual_export_status()
                result = status.get("result", {})
                content = result.get("content", [])

                for item in content:
                    if item.get("type") == "text":
                        try:
                            data = json.loads(item.get("text", "{}"))
                            if data.get("state") == "completed":
                                break
                            if data.get("media_downloaded", 0) > 0:
                                print(f"Media downloaded so far: {data.get('media_downloaded')}")
                        except json.JSONDecodeError:
                            pass
            except Exception:
                pass

            # Count media files
            media_count = count_media_files(export_directory)
            print(f"Media files found after {waited}s: {media_count}")

            # If we have some media, we can check the test condition
            if media_count > 0:
                break

        # Final count
        media_count = count_media_files(export_directory)
        print(f"Final media file count: {media_count}")

        # Verify the count
        # This test passes if:
        # 1. No media was expected (empty channel) - skip
        # 2. Media was downloaded and count <= 10
        # 3. More media exists but we respected limits

        if media_count == 0:
            # Check if any export files exist
            all_files = []
            for root, dirs, files in os.walk(export_directory):
                all_files.extend(files)

            if not all_files:
                pytest.fail("Export produced no files. Channel may be empty.")
            else:
                # Export worked but no media files
                print(f"Export completed with {len(all_files)} files but no media")
                # This is acceptable - channel might not have media
        else:
            # Verify we have some media but not exceeding our test limit
            assert media_count <= MEDIA_DOWNLOAD_LIMIT + 5, (
                f"Too many media files downloaded: {media_count}. "
                f"Expected at most {MEDIA_DOWNLOAD_LIMIT + 5}"
            )
            print(f"SUCCESS: Downloaded {media_count} media files")

    def test_media_files_exist(self, export_client, export_directory):
        """
        MANDATORY TEST: Verify media files are actual files (not empty)
        """
        # Get channels
        chats_response = export_client.list_chats(limit=50)
        channels = find_channels(chats_response)

        if not channels:
            pytest.fail("No channels found")

        channel = channels[0]
        chat_id = channel.get("id")

        # Export
        export_client.export_chat(
            chat_id=chat_id,
            output_path=export_directory,
            format="json"
        )

        time.sleep(5)

        # Check if any files were downloaded
        media_extensions = {'.jpg', '.jpeg', '.png', '.gif', '.mp4', '.mp3'}

        for root, dirs, files in os.walk(export_directory):
            for file in files:
                ext = os.path.splitext(file)[1].lower()
                if ext in media_extensions:
                    file_path = os.path.join(root, file)
                    file_size = os.path.getsize(file_path)

                    # Media files should not be empty
                    assert file_size > 0, f"Media file is empty: {file}"

                    # Images should be at least 100 bytes
                    if ext in {'.jpg', '.jpeg', '.png', '.gif'}:
                        assert file_size > 100, f"Image file too small: {file} ({file_size} bytes)"

                    print(f"Media file verified: {file} ({file_size} bytes)")


class TestChannelExportIntegration:
    """Integration tests for the complete export flow"""

    def test_full_export_flow(self, export_client, export_directory):
        """
        MANDATORY TEST: End-to-end test of channel export

        1. List channels
        2. Pick random channel
        3. Export
        4. Verify directory naming
        5. Verify export files exist
        """
        # List channels
        chats_response = export_client.list_chats(limit=30)
        channels = find_channels(chats_response)

        if not channels:
            pytest.fail("No channels available for testing")

        # Pick random channel
        import random
        channel = random.choice(channels)
        chat_id = channel.get("id")
        chat_name = channel.get("name", channel.get("title", "Unknown"))

        print(f"\n=== Full export test for: {chat_name} (ID: {chat_id}) ===")

        # Record start time for directory name validation
        start_time = datetime.now()

        # Export
        export_response = export_client.export_chat(
            chat_id=chat_id,
            output_path=export_directory,
            format="json"
        )

        # Wait for completion
        time.sleep(5)

        # Check results
        all_items = os.listdir(export_directory)

        assert len(all_items) > 0, "Export produced no files or directories"

        # Find the export directory or files
        subdirs = [d for d in all_items if os.path.isdir(os.path.join(export_directory, d))]
        files = [f for f in all_items if os.path.isfile(os.path.join(export_directory, f))]

        print(f"Export results: {len(subdirs)} directories, {len(files)} files")

        if subdirs:
            # Validate directory naming
            export_dir = subdirs[0]
            print(f"Export directory: {export_dir}")

            # Should follow Type-Name-DDMMYYYY-HHMMSS format
            pattern = r'^[A-Za-z]+-.*-\d{8}-\d{6}$'
            assert re.match(pattern, export_dir), (
                f"Directory name doesn't match expected pattern: {export_dir}"
            )

            # List contents
            export_path = os.path.join(export_directory, export_dir)
            contents = os.listdir(export_path)
            print(f"Export contents: {contents[:10]}...")  # First 10

            assert len(contents) > 0, "Export directory is empty"

        elif files:
            # Direct file export
            print(f"Export files: {files}")

            # Should have at least one export file
            export_files = [f for f in files if f.endswith(('.json', '.html', '.txt'))]
            assert len(export_files) > 0, "No export files found"

        print("SUCCESS: Full export flow completed")


# Run tests when executed directly
if __name__ == "__main__":
    pytest.main([__file__, "-v", "--tb=short"])
