#pragma once

#include <caffeine.h>

#include <util/dstr.h>

struct caffeine_credentials
{
	struct dstr access_token;
	struct dstr refresh_token;
	struct dstr caid;
	struct dstr credential;
};

struct caffeine_auth_response {
	struct caffeine_credentials * credentials;
	struct dstr next;
	struct dstr mfa_otp_method;
};

struct caffeine_auth_response * caffeine_signin(
	char const * username,
	char const * password,
	char const * otp);

void caffeine_free_auth_response(struct caffeine_auth_response * auth_response);
