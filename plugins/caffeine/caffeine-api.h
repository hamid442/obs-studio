#pragma once

#include <caffeine.h>

struct caffeine_credentials
{
	char * access_token;
	char * refresh_token;
	char * caid;
	char * credential;
};

struct caffeine_auth_info {
	struct caffeine_credentials * credentials;
	char * next;
	char * mfa_otp_method;
};

struct caffeine_user_info {
	char * caid;
	char * username;
	char * stage_id;
	bool can_broadcast;
};

struct caffeine_auth_info * caffeine_signin(
	char const * username,
	char const * password,
	char const * otp);

void caffeine_free_auth_info(struct caffeine_auth_info * auth_info);

struct caffeine_user_info * caffeine_getuser(
	char const * caid,
	struct caffeine_auth_info * auth_info);

void caffeine_free_user_info(struct caffeine_user_info * user_info);
