#!/bin/bash
#
# Setup script for Tlgrm Update Worker
#
# Prerequisites:
#   - Node.js 18+ installed
#   - Cloudflare account with Workers plan
#   - Domain 71grm.site configured in Cloudflare DNS
#   - (Optional) GitHub personal access token for higher rate limits
#
# Usage:
#   cd cloudflare-worker
#   ./setup.sh
#

set -euo pipefail

echo "=== Tlgrm Update Worker Setup ==="
echo ""

# Check prerequisites
if ! command -v node &> /dev/null; then
    echo "ERROR: Node.js is required. Install via: brew install node"
    exit 1
fi

if ! command -v npx &> /dev/null; then
    echo "ERROR: npx is required (comes with Node.js)"
    exit 1
fi

# Install dependencies
echo "1. Installing dependencies..."
npm install

# Login to Cloudflare (if not already)
echo ""
echo "2. Checking Cloudflare authentication..."
if ! npx wrangler whoami &> /dev/null 2>&1; then
    echo "   Not logged in. Running 'wrangler login'..."
    npx wrangler login
else
    echo "   Already authenticated."
fi

# Create KV namespace
echo ""
echo "3. Creating KV namespace..."
KV_OUTPUT=$(npx wrangler kv namespace create "CACHE" 2>&1 || true)
echo "$KV_OUTPUT"

# Extract the namespace ID
KV_ID=$(echo "$KV_OUTPUT" | grep -o 'id = "[^"]*"' | head -1 | grep -o '"[^"]*"' | tr -d '"')
if [ -n "$KV_ID" ]; then
    echo "   KV namespace ID: $KV_ID"
    # Update wrangler.toml
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sed -i '' "s/REPLACE_WITH_KV_NAMESPACE_ID/$KV_ID/" wrangler.toml
    else
        sed -i "s/REPLACE_WITH_KV_NAMESPACE_ID/$KV_ID/" wrangler.toml
    fi
    echo "   Updated wrangler.toml with KV namespace ID"
fi

# Create preview KV namespace
KV_PREVIEW_OUTPUT=$(npx wrangler kv namespace create "CACHE" --preview 2>&1 || true)
echo "$KV_PREVIEW_OUTPUT"

KV_PREVIEW_ID=$(echo "$KV_PREVIEW_OUTPUT" | grep -o 'id = "[^"]*"' | head -1 | grep -o '"[^"]*"' | tr -d '"')
if [ -n "$KV_PREVIEW_ID" ]; then
    echo "   Preview KV namespace ID: $KV_PREVIEW_ID"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sed -i '' "s/REPLACE_WITH_PREVIEW_KV_NAMESPACE_ID/$KV_PREVIEW_ID/" wrangler.toml
    else
        sed -i "s/REPLACE_WITH_PREVIEW_KV_NAMESPACE_ID/$KV_PREVIEW_ID/" wrangler.toml
    fi
    echo "   Updated wrangler.toml with preview KV namespace ID"
fi

# Prompt for GitHub token
echo ""
echo "4. GitHub Token (optional but recommended)"
echo "   Without a token: 60 requests/hour rate limit"
echo "   With a token:    5,000 requests/hour rate limit"
echo ""
read -p "   Enter GitHub personal access token (or press Enter to skip): " GITHUB_TOKEN

if [ -n "$GITHUB_TOKEN" ]; then
    echo "$GITHUB_TOKEN" | npx wrangler secret put GITHUB_TOKEN
    echo "   GitHub token stored as Worker secret"
else
    echo "   Skipped. You can add it later with: npx wrangler secret put GITHUB_TOKEN"
fi

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Next steps:"
echo "  1. Test locally:      npm run dev"
echo "  2. Deploy:            npm run deploy"
echo "  3. Set custom domain: Cloudflare Dashboard > Workers > tlgrm-updates > Triggers > Custom Domains > Add 'updates.71grm.site'"
echo ""
echo "DNS setup (in Cloudflare Dashboard > DNS):"
echo "  If you use Custom Domains (recommended): Cloudflare handles it automatically."
echo "  If you use routes: Add a CNAME record:"
echo "    updates.71grm.site -> tlgrm-updates.<your-subdomain>.workers.dev"
echo ""
echo "Verify deployment:"
echo "  curl https://updates.71grm.site/health"
echo "  curl https://updates.71grm.site/current"
echo ""
echo "Configure Tlgrm to use this update server:"
echo "  Write 'https://updates.71grm.site' to the autoupdate prefix file,"
echo "  or set it programmatically via Local::writeAutoupdatePrefix()"
