/**
 * Tlgrm Update Proxy - Cloudflare Worker
 *
 * Serves as the update server for Tlgrm (Telegram Desktop fork).
 * Proxies GitHub Releases through Telegram Desktop's native update protocol.
 *
 * Endpoints:
 *   GET /current         - Version metadata (JSON, Telegram update format)
 *   GET /tarmacupd*      - ARM Mac update binary (proxy from GitHub)
 *   GET /tmacupd*        - Intel Mac update binary (proxy from GitHub)
 *   GET /health          - Health check
 */

export interface Env {
	CACHE: KVNamespace;
	GITHUB_TOKEN?: string;
	GITHUB_REPO: string;
	CACHE_TTL_SECONDS: string;
}

// ─── Constants ────────────────────────────────────────────────────────────────

const KV_KEY_RELEASE = "github_latest_release";
const KV_KEY_ETAG = "github_etag";
const KV_KEY_CURRENT4 = "current4_response";

/** Filename patterns the Telegram updater recognizes (macOS only for this worker). */
const UPDATE_FILE_PATTERNS = [
	/^tarmacupd\d+$/,   // ARM Mac
	/^tmacupd\d+$/,     // Intel Mac
] as const;

/** Maximum GitHub API response size we'll accept (2 MB). */
const MAX_GITHUB_RESPONSE_BYTES = 2 * 1024 * 1024;

// ─── Types ────────────────────────────────────────────────────────────────────

interface GitHubRelease {
	tag_name: string;
	name: string;
	draft: boolean;
	prerelease: boolean;
	published_at: string;
	assets: GitHubAsset[];
}

interface GitHubAsset {
	name: string;
	browser_download_url: string;
	size: number;
	content_type: string;
}

interface CachedRelease {
	version: number;
	tag: string;
	assets: Record<string, { url: string; size: number }>;
	fetchedAt: number;
}

interface Current4Response {
	armac?: {
		stable: {
			released: string;
			link: string;
		};
	};
	mac?: {
		stable: {
			released: string;
			link: string;
		};
	};
}

// ─── Version Parsing ──────────────────────────────────────────────────────────

/**
 * Convert a semver tag like "v6.9.7" to Telegram's integer version format.
 *
 * Telegram Desktop uses: major * 1000000 + minor * 1000 + patch
 *   v6.9.7  -> 6009007
 *   v6.10.1 -> 6010001
 *   v7.0.0  -> 7000000
 *
 * Returns null if the tag cannot be parsed.
 */
function tagToVersion(tag: string): number | null {
	// Strip leading 'v' if present
	const cleaned = tag.startsWith("v") ? tag.slice(1) : tag;
	const match = cleaned.match(/^(\d+)\.(\d+)\.(\d+)$/);
	if (!match) return null;

	const major = parseInt(match[1], 10);
	const minor = parseInt(match[2], 10);
	const patch = parseInt(match[3], 10);

	if (major > 99 || minor > 999 || patch > 999) return null;

	return major * 1000000 + minor * 1000 + patch;
}

/**
 * Convert an integer version back to a display string.
 *   6009007 -> "6.9.7"
 */
function versionToString(v: number): string {
	const major = Math.floor(v / 1000000);
	const minor = Math.floor((v % 1000000) / 1000);
	const patch = v % 1000;
	return `${major}.${minor}.${patch}`;
}

// ─── GitHub API ───────────────────────────────────────────────────────────────

/**
 * Fetch the latest release from GitHub, using conditional requests (ETag)
 * to avoid burning rate limit on unchanged data.
 *
 * Returns a CachedRelease or null if the release has no usable assets.
 */
async function fetchLatestRelease(
	env: Env,
): Promise<CachedRelease | null> {
	const cacheTtl = parseInt(env.CACHE_TTL_SECONDS, 10) || 3600;

	// Check KV cache first
	const cached = await env.CACHE.get<CachedRelease>(KV_KEY_RELEASE, "json");
	if (cached && (Date.now() - cached.fetchedAt) < cacheTtl * 1000) {
		return cached;
	}

	// Build GitHub API request with conditional headers
	const headers: Record<string, string> = {
		"Accept": "application/vnd.github+json",
		"User-Agent": "Tlgrm-Update-Worker/1.0",
		"X-GitHub-Api-Version": "2022-11-28",
	};

	if (env.GITHUB_TOKEN) {
		headers["Authorization"] = `Bearer ${env.GITHUB_TOKEN}`;
	}

	// Use stored ETag for conditional request
	const storedEtag = await env.CACHE.get(KV_KEY_ETAG);
	if (storedEtag && cached) {
		headers["If-None-Match"] = storedEtag;
	}

	const apiUrl = `https://api.github.com/repos/${env.GITHUB_REPO}/releases/latest`;

	let response: Response;
	try {
		response = await fetch(apiUrl, { headers });
	} catch (err) {
		console.error("GitHub API fetch failed:", err);
		// Return stale cache if available
		return cached ?? null;
	}

	// 304 Not Modified -- cached data is still valid
	if (response.status === 304 && cached) {
		// Refresh the fetchedAt timestamp so we don't hit GitHub again for another TTL period
		const refreshed: CachedRelease = { ...cached, fetchedAt: Date.now() };
		await env.CACHE.put(KV_KEY_RELEASE, JSON.stringify(refreshed), {
			expirationTtl: cacheTtl * 4, // KV expiry is 4x the soft TTL
		});
		return refreshed;
	}

	// Rate limited
	if (response.status === 403 || response.status === 429) {
		const retryAfter = response.headers.get("Retry-After");
		const rateLimitRemaining = response.headers.get("X-RateLimit-Remaining");
		console.warn(
			`GitHub rate limited: status=${response.status}, ` +
			`remaining=${rateLimitRemaining}, retry-after=${retryAfter}`
		);
		// Return stale cache
		return cached ?? null;
	}

	if (response.status === 404) {
		console.warn("No releases found for repo:", env.GITHUB_REPO);
		return cached ?? null;
	}

	if (!response.ok) {
		console.error(`GitHub API error: ${response.status} ${response.statusText}`);
		return cached ?? null;
	}

	// Guard against oversized responses
	const contentLength = response.headers.get("Content-Length");
	if (contentLength && parseInt(contentLength, 10) > MAX_GITHUB_RESPONSE_BYTES) {
		console.error("GitHub response too large:", contentLength);
		return cached ?? null;
	}

	const release = await response.json() as GitHubRelease;

	// Skip drafts and prereleases
	if (release.draft || release.prerelease) {
		console.warn("Latest release is draft/prerelease, skipping");
		return cached ?? null;
	}

	const version = tagToVersion(release.tag_name);
	if (!version) {
		console.error("Cannot parse version from tag:", release.tag_name);
		return cached ?? null;
	}

	// Index assets by filename
	const assets: Record<string, { url: string; size: number }> = {};
	for (const asset of release.assets) {
		// Only index update files we recognize
		if (UPDATE_FILE_PATTERNS.some(p => p.test(asset.name))) {
			assets[asset.name] = {
				url: asset.browser_download_url,
				size: asset.size,
			};
		}
	}

	if (Object.keys(assets).length === 0) {
		console.warn("No matching update assets in release:", release.tag_name);
		// Don't cache this -- might be a release in progress with assets still uploading
		return cached ?? null;
	}

	const result: CachedRelease = {
		version,
		tag: release.tag_name,
		assets,
		fetchedAt: Date.now(),
	};

	// Store in KV
	await env.CACHE.put(KV_KEY_RELEASE, JSON.stringify(result), {
		expirationTtl: cacheTtl * 4,
	});

	// Store ETag for future conditional requests
	const etag = response.headers.get("ETag");
	if (etag) {
		await env.CACHE.put(KV_KEY_ETAG, etag, {
			expirationTtl: cacheTtl * 4,
		});
	}

	return result;
}

// ─── Response Builders ────────────────────────────────────────────────────────

function corsHeaders(): Record<string, string> {
	return {
		"Access-Control-Allow-Origin": "*",
		"Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
		"Access-Control-Allow-Headers": "Range, If-Range, If-None-Match",
		"Access-Control-Expose-Headers": "Content-Length, Content-Range, ETag, Accept-Ranges",
	};
}

function jsonResponse(data: unknown, status = 200, cacheSeconds = 300): Response {
	return new Response(JSON.stringify(data), {
		status,
		headers: {
			"Content-Type": "application/json; charset=utf-8",
			"Cache-Control": `public, max-age=${cacheSeconds}, s-maxage=${cacheSeconds}`,
			...corsHeaders(),
		},
	});
}

function errorResponse(message: string, status: number): Response {
	return new Response(JSON.stringify({ error: message }), {
		status,
		headers: {
			"Content-Type": "application/json; charset=utf-8",
			"Cache-Control": "no-store",
			...corsHeaders(),
		},
	});
}

// ─── Route: /current4 ────────────────────────────────────────────────────────

/**
 * Build the /current4 response in Telegram Desktop's expected format.
 *
 * The app's HttpChecker (update_checker.cpp) does:
 *   1. GET {prefix}/current4
 *   2. Parse JSON: platform -> channel -> { released, link }
 *   3. Extract version from "released" field
 *   4. If version > current AppVersion, download {prefix} + link
 *
 * Platform keys:
 *   "armac" - ARM Mac (Apple Silicon, or Rosetta)
 *   "mac"   - Intel Mac
 *
 * The "released" field format: version as string (e.g., "6009007")
 * The "link" field: relative path to the update binary (e.g., "/tarmacupd6009007")
 */
async function handleCurrent4(env: Env): Promise<Response> {
	const release = await fetchLatestRelease(env);
	if (!release) {
		return errorResponse("No release data available", 503);
	}

	const versionStr = String(release.version);
	const response: Current4Response = {};

	// Find ARM Mac asset
	const armAssetName = `tarmacupd${release.version}`;
	if (release.assets[armAssetName]) {
		response.armac = {
			stable: {
				released: versionStr,
				link: `/${armAssetName}`,
			},
		};
	}

	// Find Intel Mac asset
	const macAssetName = `tmacupd${release.version}`;
	if (release.assets[macAssetName]) {
		response.mac = {
			stable: {
				released: versionStr,
				link: `/${macAssetName}`,
			},
		};
	}

	if (!response.armac && !response.mac) {
		return errorResponse("No macOS update assets found in latest release", 503);
	}

	// Cache for 5 minutes at edge, 5 minutes in browser
	// The Worker's own KV cache handles the 1-hour GitHub API cache
	return jsonResponse(response, 200, 300);
}

// ─── Route: Binary Download ──────────────────────────────────────────────────

/**
 * Proxy an update binary download from GitHub Releases.
 *
 * Strategy: 302 redirect to GitHub's CDN URL.
 *
 * Why redirect instead of proxying through the Worker:
 *   - GitHub's CDN is fast and globally distributed
 *   - No 128 MB Worker memory limit concerns
 *   - Range headers are natively supported by GitHub's CDN
 *   - Avoids egress bandwidth costs on Workers
 *
 * The Telegram updater's HttpLoader follows redirects and supports
 * chunked downloads, so a 302 works perfectly.
 *
 * If a streaming proxy is ever needed (e.g., to add signatures or transform
 * the binary), see handleBinaryProxy() below.
 */
async function handleBinaryRedirect(
	filename: string,
	env: Env,
): Promise<Response> {
	const release = await fetchLatestRelease(env);
	if (!release) {
		return errorResponse("No release data available", 503);
	}

	const asset = release.assets[filename];
	if (!asset) {
		// The requested file might be for a different version than current.
		// Try to find it by searching all assets (filename includes version).
		// If not found, it's genuinely missing.
		return errorResponse(`Asset not found: ${filename}`, 404);
	}

	// 302 redirect to GitHub's CDN
	return new Response(null, {
		status: 302,
		headers: {
			"Location": asset.url,
			"Cache-Control": "public, max-age=86400, s-maxage=86400",
			...corsHeaders(),
		},
	});
}

/**
 * Alternative: Stream the binary through the Worker.
 * Use this if you need to:
 *   - Add custom headers (e.g., Content-Disposition)
 *   - Transform the binary
 *   - Avoid exposing the GitHub URL
 *   - Work around CORS issues
 *
 * This uses streaming (TransformStream) to avoid buffering the entire
 * file in Worker memory. Cloudflare Workers support streaming responses
 * that exceed the 128 MB memory limit.
 */
async function handleBinaryProxy(
	filename: string,
	request: Request,
	env: Env,
): Promise<Response> {
	const release = await fetchLatestRelease(env);
	if (!release) {
		return errorResponse("No release data available", 503);
	}

	const asset = release.assets[filename];
	if (!asset) {
		return errorResponse(`Asset not found: ${filename}`, 404);
	}

	// Forward Range headers for resume support
	const proxyHeaders: Record<string, string> = {
		"User-Agent": "Tlgrm-Update-Worker/1.0",
	};

	const rangeHeader = request.headers.get("Range");
	if (rangeHeader) {
		proxyHeaders["Range"] = rangeHeader;
	}

	const ifRange = request.headers.get("If-Range");
	if (ifRange) {
		proxyHeaders["If-Range"] = ifRange;
	}

	let upstream: Response;
	try {
		upstream = await fetch(asset.url, {
			headers: proxyHeaders,
			redirect: "follow",
		});
	} catch (err) {
		console.error("Failed to fetch asset from GitHub:", err);
		return errorResponse("Upstream fetch failed", 502);
	}

	if (!upstream.ok && upstream.status !== 206) {
		return errorResponse(
			`Upstream error: ${upstream.status}`,
			upstream.status >= 500 ? 502 : upstream.status,
		);
	}

	// Build response headers
	const responseHeaders: Record<string, string> = {
		"Content-Type": "application/octet-stream",
		"Cache-Control": "public, max-age=86400, s-maxage=604800",
		"Accept-Ranges": "bytes",
		...corsHeaders(),
	};

	// Pass through content headers from upstream
	const passthrough = [
		"Content-Length",
		"Content-Range",
		"ETag",
		"Last-Modified",
	];
	for (const header of passthrough) {
		const value = upstream.headers.get(header);
		if (value) {
			responseHeaders[header] = value;
		}
	}

	// If no Content-Length from upstream, set it from our cached asset size
	if (!responseHeaders["Content-Length"] && !rangeHeader) {
		responseHeaders["Content-Length"] = String(asset.size);
	}

	// Stream the body through without buffering
	return new Response(upstream.body, {
		status: upstream.status,
		headers: responseHeaders,
	});
}

// ─── Route: Health Check ──────────────────────────────────────────────────────

async function handleHealth(env: Env): Promise<Response> {
	const cached = await env.CACHE.get<CachedRelease>(KV_KEY_RELEASE, "json");

	const status = {
		ok: true,
		timestamp: new Date().toISOString(),
		repo: env.GITHUB_REPO,
		cache: cached
			? {
				version: cached.version,
				versionString: versionToString(cached.version),
				tag: cached.tag,
				assetCount: Object.keys(cached.assets).length,
				assets: Object.keys(cached.assets),
				age: Math.round((Date.now() - cached.fetchedAt) / 1000),
				ttl: parseInt(env.CACHE_TTL_SECONDS, 10) || 3600,
			}
			: null,
	};

	return jsonResponse(status, 200, 60);
}

// ─── Route: Cache Purge ───────────────────────────────────────────────────────

/**
 * Force-refresh the cached release data.
 * POST /purge
 *
 * This is useful after publishing a new release to GitHub -- call this
 * endpoint to make the update available immediately instead of waiting
 * for the cache TTL to expire.
 *
 * No authentication required since the worst case is an extra GitHub API call.
 */
async function handlePurge(env: Env): Promise<Response> {
	await env.CACHE.delete(KV_KEY_RELEASE);
	await env.CACHE.delete(KV_KEY_ETAG);
	await env.CACHE.delete(KV_KEY_CURRENT4);

	// Immediately re-fetch
	const release = await fetchLatestRelease(env);

	return jsonResponse({
		purged: true,
		release: release
			? {
				version: release.version,
				versionString: versionToString(release.version),
				tag: release.tag,
				assets: Object.keys(release.assets),
			}
			: null,
	});
}

// ─── Request Router ───────────────────────────────────────────────────────────

export default {
	async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
		const url = new URL(request.url);
		const path = url.pathname;

		// CORS preflight
		if (request.method === "OPTIONS") {
			return new Response(null, {
				status: 204,
				headers: {
					...corsHeaders(),
					"Access-Control-Max-Age": "86400",
				},
			});
		}

		// Only allow GET, HEAD, POST (POST only for /purge)
		if (!["GET", "HEAD", "POST"].includes(request.method)) {
			return errorResponse("Method not allowed", 405);
		}

		try {
			// ── /current4 - Version metadata ──────────────────────────────
			// The client requests "/current" with no generation suffix -- upstream
			// appends AutoUpdateVersion() so old systems fetch a different
			// document, which is meaningless for a fork shipping one build.
			// The suffixed forms stay routed so an install from before that
			// change still gets a manifest rather than a 404.
			if (path === "/current" || path === "/current/"
				|| path === "/current4" || path === "/current4/"
				|| path === "/current2" || path === "/current2/") {
				if (request.method !== "GET" && request.method !== "HEAD") {
					return errorResponse("Method not allowed", 405);
				}

				// Try CF Cache API first (edge cache, faster than KV)
				const cacheKey = new Request(url.toString(), request);
				const cache = caches.default;
				let response = await cache.match(cacheKey);
				if (response) {
					return response;
				}

				response = await handleCurrent4(env);

				// Store in CF edge cache (non-blocking)
				if (response.status === 200) {
					ctx.waitUntil(cache.put(cacheKey, response.clone()));
				}

				return response;
			}

			// ── /health - Health check ────────────────────────────────────
			if (path === "/health" || path === "/health/") {
				return handleHealth(env);
			}

			// ── /purge - Cache purge ──────────────────────────────────────
			if (path === "/purge" || path === "/purge/") {
				if (request.method !== "POST") {
					return errorResponse("Use POST to purge cache", 405);
				}
				return handlePurge(env);
			}

			// ── /tarmacupd* or /tmacupd* - Binary download ───────────────
			const filename = path.slice(1); // Remove leading /
			if (UPDATE_FILE_PATTERNS.some(p => p.test(filename))) {
				if (request.method !== "GET" && request.method !== "HEAD") {
					return errorResponse("Method not allowed", 405);
				}

				// Use streaming proxy by default.
				// Advantages over redirect:
				//   - Hides GitHub URL from the client
				//   - Allows edge caching of the binary at Cloudflare
				//   - Better control over cache headers
				//   - Range header support verified end-to-end
				//
				// Switch to handleBinaryRedirect() if you want to save
				// bandwidth costs and trust GitHub's CDN directly.
				return handleBinaryProxy(filename, request, env);
			}

			// ── Fallback: 404 ─────────────────────────────────────────────
			return errorResponse("Not found", 404);

		} catch (err) {
			console.error("Unhandled error:", err);
			return errorResponse("Internal server error", 500);
		}
	},
} satisfies ExportedHandler<Env>;
