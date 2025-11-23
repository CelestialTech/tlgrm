.PHONY: help setup build run test clean

# Colors for output
BLUE := \033[0;34m
GREEN := \033[0;32m
YELLOW := \033[0;33m
NC := \033[0m # No Color

help:
	@echo "$(BLUE)Telegram MCP - Unified Build System$(NC)"
	@echo ""
	@echo "$(GREEN)Setup:$(NC)"
	@echo "  make setup              - Install all dependencies (C++ + Python)"
	@echo "  make setup-cpp          - Install C++ dependencies only"
	@echo "  make setup-python       - Install Python minimal dependencies"
	@echo "  make setup-python-full  - Install Python with AI/ML"
	@echo ""
	@echo "$(GREEN)Build:$(NC)"
	@echo "  make build              - Build all components"
	@echo "  make build-cpp          - Build C++ (Telegram Desktop)"
	@echo "  make build-python       - Verify Python setup"
	@echo ""
	@echo "$(GREEN)Run:$(NC)"
	@echo "  make run-cpp            - Run C++ MCP server (Telegram Desktop)"
	@echo "  make run-python         - Run Python MCP (hybrid mode, with AI/ML)"
	@echo "  make run-python-minimal - Run Python MCP (bridge mode, no AI/ML)"
	@echo "  make run-all            - Run both C++ and Python servers"
	@echo ""
	@echo "$(GREEN)Test:$(NC)"
	@echo "  make test               - Run all tests"
	@echo "  make test-cpp           - Run C++ tests"
	@echo "  make test-python        - Run Python tests"
	@echo ""
	@echo "$(GREEN)Utilities:$(NC)"
	@echo "  make clean              - Remove build artifacts"
	@echo "  make status             - Show component status"
	@echo "  make logs               - Tail all logs"

# ==============================================================================
# Setup
# ==============================================================================

setup: setup-cpp setup-python
	@echo "$(GREEN)✅ All dependencies installed$(NC)"

setup-cpp:
	@echo "$(BLUE)Installing C++ dependencies...$(NC)"
	cd tdesktop && ./configure.sh
	@echo "$(GREEN)✅ C++ setup complete$(NC)"

setup-python:
	@echo "$(BLUE)Installing Python (minimal) dependencies with uv...$(NC)"
	cd pythonMCP && uv pip install -r requirements-minimal.txt
	@echo "$(GREEN)✅ Python (minimal) setup complete$(NC)"

setup-python-full:
	@echo "$(BLUE)Installing Python (full AI/ML) dependencies with uv...$(NC)"
	@echo "$(YELLOW)⚠️  This may take 5-10 minutes (uv is fast!)...$(NC)"
	cd pythonMCP && uv pip install -r requirements.txt
	@echo "$(GREEN)✅ Python (full) setup complete$(NC)"

# ==============================================================================
# Build
# ==============================================================================

build: build-cpp build-python
	@echo "$(GREEN)✅ All components built$(NC)"

build-cpp:
	@echo "$(BLUE)Building C++ (Telegram Desktop)...$(NC)"
	cd tdesktop && xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release
	@echo "$(GREEN)✅ C++ build complete$(NC)"

build-python:
	@echo "$(BLUE)Verifying Python setup...$(NC)"
	cd pythonMCP && python -c "import src; print('Python OK')"
	@echo "$(GREEN)✅ Python ready$(NC)"

# ==============================================================================
# Run
# ==============================================================================

run-cpp:
	@echo "$(BLUE)Starting C++ MCP server (Telegram Desktop)...$(NC)"
	./out/Release/Telegram.app/Contents/MacOS/Telegram

run-python:
	@echo "$(BLUE)Starting Python MCP (hybrid mode with AI/ML)...$(NC)"
	cd pythonMCP && python src/mcp_server.py --mode hybrid

run-python-minimal:
	@echo "$(BLUE)Starting Python MCP (bridge mode, no AI/ML)...$(NC)"
	cd pythonMCP && python src/mcp_server.py --mode bridge --no-aiml

run-python-standalone:
	@echo "$(BLUE)Starting Python MCP (standalone mode)...$(NC)"
	cd pythonMCP && python src/mcp_server.py --mode standalone

run-all:
	@echo "$(BLUE)Starting both C++ and Python servers...$(NC)"
	@echo "$(YELLOW)Note: This requires two terminals. Use 'make run-all-tmux' instead.$(NC)"
	@echo ""
	@echo "Terminal 1: make run-cpp"
	@echo "Terminal 2: make run-python"

run-all-tmux:
	@echo "$(BLUE)Starting both servers in tmux...$(NC)"
	tmux new-session -d -s telegram-mcp \; \
		send-keys 'make run-cpp' C-m \; \
		split-window -v \; \
		send-keys 'make run-python' C-m \; \
		attach

# ==============================================================================
# Test
# ==============================================================================

test: test-cpp test-python
	@echo "$(GREEN)✅ All tests passed$(NC)"

test-cpp:
	@echo "$(BLUE)Running C++ tests...$(NC)"
	cd tdesktop && xcodebuild test -project Telegram.xcodeproj -scheme Telegram
	@echo "$(GREEN)✅ C++ tests passed$(NC)"

test-python:
	@echo "$(BLUE)Running Python tests...$(NC)"
	cd pythonMCP && pytest tests/ -v
	@echo "$(GREEN)✅ Python tests passed$(NC)"

test-python-coverage:
	@echo "$(BLUE)Running Python tests with coverage...$(NC)"
	cd pythonMCP && pytest tests/ -v --cov=src --cov-report=html
	@echo "$(GREEN)✅ Coverage report: pythonMCP/htmlcov/index.html$(NC)"

# ==============================================================================
# Utilities
# ==============================================================================

clean:
	@echo "$(BLUE)Cleaning build artifacts...$(NC)"
	@echo "Cleaning C++..."
	cd tdesktop && xcodebuild clean || true
	@echo "Cleaning Python..."
	find pythonMCP -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
	find pythonMCP -type f -name "*.pyc" -delete 2>/dev/null || true
	find pythonMCP -type d -name "*.egg-info" -exec rm -rf {} + 2>/dev/null || true
	rm -rf pythonMCP/.pytest_cache pythonMCP/htmlcov 2>/dev/null || true
	@echo "$(GREEN)✅ Clean complete$(NC)"

status:
	@echo "$(BLUE)Component Status:$(NC)"
	@echo ""
	@echo "$(GREEN)C++ Server:$(NC)"
	@if [ -f "./out/Release/Telegram.app/Contents/MacOS/Telegram" ]; then \
		echo "  ✅ Built"; \
	else \
		echo "  ❌ Not built (run: make build-cpp)"; \
	fi
	@echo ""
	@echo "$(GREEN)Python Server:$(NC)"
	@cd pythonMCP && python -c "import sys; print(f'  Python: {sys.version.split()[0]}')" 2>/dev/null || echo "  ❌ Python not available"
	@cd pythonMCP && python -c "import src; print('  ✅ Core modules OK')" 2>/dev/null || echo "  ❌ Core modules missing"
	@cd pythonMCP && python -c "from src.aiml.service import is_aiml_available; print('  ✅ AI/ML available' if is_aiml_available() else '  ⚠️  AI/ML not installed (minimal mode)')" 2>/dev/null || echo "  ⚠️  AI/ML not installed"
	@echo ""
	@echo "$(GREEN)IPC:$(NC)"
	@if [ -S "/tmp/telegram_mcp.sock" ]; then \
		echo "  ✅ Socket exists: /tmp/telegram_mcp.sock"; \
	else \
		echo "  ⚠️  Socket not found (C++ server not running)"; \
	fi

logs:
	@echo "$(BLUE)Tailing logs...$(NC)"
	tail -f pythonMCP/*.log tdesktop/*.log 2>/dev/null || echo "No log files found"

# ==============================================================================
# Development
# ==============================================================================

dev-python:
	@echo "$(BLUE)Starting Python development server with auto-reload...$(NC)"
	cd pythonMCP && watchmedo auto-restart -d src -p '*.py' -- python src/mcp_server.py --mode hybrid

format-python:
	@echo "$(BLUE)Formatting Python code...$(NC)"
	cd pythonMCP && black src/ tests/
	cd pythonMCP && ruff check --fix src/ tests/
	@echo "$(GREEN)✅ Code formatted$(NC)"

lint-python:
	@echo "$(BLUE)Linting Python code...$(NC)"
	cd pythonMCP && ruff check src/ tests/
	cd pythonMCP && mypy src/
	@echo "$(GREEN)✅ Linting complete$(NC)"

# ==============================================================================
# Git
# ==============================================================================

git-status:
	@echo "$(BLUE)Git Status:$(NC)"
	git status --short

git-diff:
	@echo "$(BLUE)Git Diff:$(NC)"
	git diff
