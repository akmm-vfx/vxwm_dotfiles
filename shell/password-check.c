#define _GNU_SOURCE

#include <security/pam_appl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

struct conversation_data {
	const char *password;
	const char *username;
};

static int respond(int count, const struct pam_message **messages,
		struct pam_response **responses, void *data)
{
	struct conversation_data *input = data;
	struct pam_response *answers = calloc((size_t)count, sizeof(*answers));
	if (answers == NULL)
		return PAM_BUF_ERR;

	for (int i = 0; i < count; ++i) {
		switch (messages[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
			answers[i].resp = strdup(input->password);
			break;
		case PAM_PROMPT_ECHO_ON:
			answers[i].resp = strdup(input->username);
			break;
		case PAM_TEXT_INFO:
		case PAM_ERROR_MSG:
			answers[i].resp = NULL;
			break;
		default:
			for (int j = 0; j <= i; ++j) {
				if (answers[j].resp != NULL) {
					explicit_bzero(answers[j].resp, strlen(answers[j].resp));
					free(answers[j].resp);
				}
			}
			free(answers);
			return PAM_CONV_ERR;
		}

		if ((messages[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
		     messages[i]->msg_style == PAM_PROMPT_ECHO_ON) &&
		    answers[i].resp == NULL) {
			for (int j = 0; j <= i; ++j) {
				if (answers[j].resp != NULL) {
					explicit_bzero(answers[j].resp, strlen(answers[j].resp));
					free(answers[j].resp);
				}
			}
			free(answers);
			return PAM_BUF_ERR;
		}
	}

	*responses = answers;
	return PAM_SUCCESS;
}

int main(int argc, char **argv)
{
	if (argc != 3 || argv[1][0] == '\0' || argv[2][0] == '\0')
		return 2;

	char password[4096] = {0};
	if (fgets(password, sizeof(password), stdin) == NULL) {
		fprintf(stderr, "No password was received on standard input.\n");
		return 2;
	}
	size_t length = strcspn(password, "\r\n");
	password[length] = '\0';
	(void)mlock(password, sizeof(password));

	struct conversation_data input = {password, argv[1]};
	struct pam_conv conversation = {respond, &input};
	pam_handle_t *handle = NULL;
	int status = pam_start_confdir("vxwm-lock-screen", argv[1], &conversation,
		argv[2], &handle);
	if (status != PAM_SUCCESS) {
		fprintf(stderr, "PAM initialization failed (status %d).\n", status);
		explicit_bzero(password, sizeof(password));
		(void)munlock(password, sizeof(password));
		return 2;
	}
	status = pam_authenticate(handle, PAM_DISALLOW_NULL_AUTHTOK);
	if (status != PAM_SUCCESS)
		fprintf(stderr, "PAM authentication failed: %s (status %d).\n",
			pam_strerror(handle, status), status);
	if (handle != NULL)
		pam_end(handle, status);

	explicit_bzero(password, sizeof(password));
	(void)munlock(password, sizeof(password));
	return status == PAM_SUCCESS ? 0 : 1;
}
