#pragma once

#include <caffeine.h>

#include <util/dstr.h>

struct caffeine_creds
{
	struct dstr access_token;
	struct dstr refresh_token;
	struct dstr caid;
	struct dstr credential;
} caffeine_creds;

struct caffeine_auth_response {
	struct caffeine_creds * creds;
	/* TODO:
	 * .next
	 * .mfa_otp_method
	 * .errors
	 */
};

struct caffeine_auth_response * caffeine_signin(char const * username,
	char const * password);

void caffeine_free_auth_response(struct caffeine_auth_response * auth_response);
