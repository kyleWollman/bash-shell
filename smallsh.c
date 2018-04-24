/************************************************************
* Author: Written by Kyle Wollman (wollmank@oregonstate.edu)
* Date: 03/04/2018
* Class: CS344 400 W2018
* Description: Program 3 - smallsh: a simplified shell program
*				that has the built-in commands exit, cd, and 
*				status. For all other commands it forks off a
*				new process and calls execvp() to complete them.
**************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

int foreground;

void changeDir(char* args[])
{
	int ret;
	
	//if no directory name is provided set dir to HOME
	if(args[1] == NULL)
		ret = chdir(getenv("HOME"));
	
	else
		ret = chdir(args[1]);
}

void SIGTSTP_handler(int signal)
{
	//if foreground mode is already activated turn it off
	if(foreground)
	{
		write(1, "Exiting foreground-only mode\n: ", 31);
		foreground = 0;
	}
	
	else
	{
		write(1, "Entering foreground-only mode (& is now ignored)\n: ", 51);
	
		foreground = 1;
	}
	
	fflush(stdout);
}

	
int main(){
	char *input = NULL, *token;
	char* args[512], toToken[2048];
	char space[] = {" "};
	size_t inputSize = sizeof(input);
	int numCharsEntered, status = -5, bgExitStatus = -5;
	int i, j, ret = 0, background = 0, exitSig = 0, source = -5, destination = -5, redirect = 0, childExitStatus = -5;
	pid_t spawnPid = -5, bgPid = -5, pid;
	foreground = 0;
	
	int bgProc[20]; //array of background processes to be killed when exit is called
	int numBGProcs = 0;
	
	//SIGINT sigaction
	struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = SIG_IGN; //parent will ignore SIGINT
	sigaction(SIGINT, &SIGINT_action, NULL);
	
	//SIGTSTP sigaction
	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = SIGTSTP_handler; //parent will (de)activate foreground only mode
	SIGTSTP_action.sa_flags = SA_RESTART; //set flag to restart system after handler finishes
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
	
	do
	{

		ret = waitpid(bgPid, &bgExitStatus, WNOHANG); //check status of background processes
		
		if(ret != 0) //if the status of any background processes has changed
		{
			pid = wait(&bgExitStatus); //kill defunct processes
			
			if(bgExitStatus == 0) //normal exit
			{
				printf("background pid %d is done: exit value %d\n", bgPid, bgExitStatus);
				fflush(stdout);
				
				bgExitStatus = -5;
			}
			
			else if(bgExitStatus > 0) //signal killed
			{
				printf("background pid %d is done: terminated by signal %d\n", bgPid, bgExitStatus);
				fflush(stdout);
				
				bgExitStatus = -5;
			}
		}
		
		//reset flags
		redirect = 0;
		source = destination = -5;
		background = 0;
		
		//clear user input
		if(input)
		{
			free(input);
			input = NULL;
		}
		
		printf(": ");
		fflush(stdout);
	
		numCharsEntered = getline(&input, &inputSize, stdin);
		
		
		if(input && strcmp(input, "exit\n") == 0)
		{ 
			//kill background processes before exiting
			for(i = 0; i < numBGProcs; i++)
			{
				kill(bgProc[i], 15);
			}
			
			//set exit flag
			exitSig = 1;
		}
		
		if(numCharsEntered > 1 && !exitSig)
		{
			input[numCharsEntered - 1] = '\0'; //remove '\n'
			strcpy(toToken, input); //move input into toToken to preserve input
			
			token = strtok(toToken, space);//get first arg
			
			i = 0; //token counter
			
			while(token != NULL)
			{
				args[i] = token;
				token = strtok(NULL, space);
				i++;
			}
			
			//check for redirect and variable expansion
			for(j = 0; j < i; j++)
			{
				if(strcmp(args[j],"<") == 0)
				{
					redirect = 1;
					source = j + 1;
				}
				
				else if(strcmp(args[j], ">") == 0)
				{
					redirect = 1;
					destination = j + 1;
				}
				
				else if(strstr(args[j], "$$") != NULL)
				{
					char *c;
					char tempStr[strlen(args[j])];
					c = strstr(args[j], "$$"); //get pointer to '$$'
					strncpy(tempStr, args[j], c - args[j]); //copy up to pointer into tempStr
					tempStr[c - args[j]] = '\0';
					sprintf(tempStr+(c - args[j]), "%d%s", getpid(), c + 2); //rebuild string with pid in place of '$$'
					
					strcpy(args[j], tempStr); //move back into args[j]
				}
			}
			
			if(strcmp(args[i - 1], "&") == 0)
			{
				background = 1;
				args[i - 1] = NULL; //replace '&' with NULL
			}
			
			if(input[0] == '#')
				write(1, "\n", 1); //skip comment lines
			
			else if(strcmp(args[0], "cd") == 0)
				changeDir(args);
			
			else if(strcmp(args[0], "status") == 0)
			{
				if(status < 2) //for 0 and 1 status print exit value
				{
					printf("exit value %d\n", status);
					fflush(stdout);
				}
				
				else //for higher status print signal number
				{
					printf("terminated by signal %d\n", status);
					fflush(stdout);
				}
			}
			
			else
			{
				spawnPid = fork(); //create new process
				
				switch(spawnPid)
				{
					case -1: { perror("Error spawning\n"); exit(1); break; }
					
					case 0: 
					{	
						if(background && !foreground)
						{
							
							//background processes will ignore SIGINT
							struct sigaction SIGINT_ch_action = {0};
							SIGINT_ch_action.sa_handler = SIG_IGN;
							sigaction(SIGINT, &SIGINT_ch_action, NULL);
							
							//redirect stdin to dev/null for bg when no other source is given
							if(source < 0)
							{
								int devNULL = open("/dev/null", O_RDONLY);
								int sourceRD = dup2(devNULL, 0);
							}
							
							//redirect stdout to dev/null for bg when no other source is given
							if(destination < 0)
							{
								int devNULL = open("/dev/null", O_WRONLY);
								int destRD = dup2(devNULL, 1);
							}
						}
						
						else if(!background)
						{
							//foreground processes will be terminated by SIGINT
							struct sigaction SIGINT_ch_action = {0};
							SIGINT_ch_action.sa_handler = SIG_DFL;
							sigaction(SIGINT, &SIGINT_ch_action, NULL);
						}
							
						if(redirect)
						{
							if(source >= 0)
							{
								int sourceFD = open(args[source], O_RDONLY);
								int sourceResult = dup2(sourceFD, 0); //redirect stdin
								
								if(sourceResult == -1) //if bad file
								{
									printf("cannot open %s for input\n", args[source]);
									fflush(stdout);
									exit(1);
								}
								
								args[source - 1] = NULL; //replace '<' with NULL
								close(sourceFD);
							}
							
							if(destination >= 0)
							{
								int destFD = open(args[destination], O_WRONLY | O_CREAT| O_TRUNC, S_IRUSR | S_IWUSR);
								int destResult = dup2(destFD, 1);
								
								if(destResult == -1)//if bad file
								{
									perror("no such file or directory");
									fflush(stdout);
									exit(1);
								}
								
								args[destination - 1] = NULL; //replace '>' with NULL
								close(destFD);
							}
						}
						
						args[i] = NULL; //tell exec when to stop reading
						
						execvp(*args, args);
						
						//if execvp fails printout the command that caused failure and the reason
						perror(input);
						
						fflush(stdout);
						
						//exit status to one if execvp fails
						exit(1);
						
						break;
					}
					
					//parent
					default:
					{
						if(background && !foreground)
						{
							bgPid = spawnPid; //store background process id
							bgProc[numBGProcs] = bgPid;
							numBGProcs++;
				
							printf("background pid is %d\n", spawnPid);
							fflush(stdout);
							
							background = 0; //reset flag
						
						}
						
						else
						{
							spawnPid = waitpid(spawnPid, &childExitStatus, 0);
							
							if(WIFSIGNALED(childExitStatus) != 0) //if child killed by signal
							{
								status = WTERMSIG(childExitStatus);
								printf("terminated by signal %d\n", status);
								fflush(stdout);
							}

							else if(WIFEXITED(childExitStatus)) //if child exited normally
							{
								status = WEXITSTATUS(childExitStatus);	
		
							}
						}
						
						break;
					}
				}
			}
			
			//clear args array
			for(j = 0; j < i; j++)
			{
				args[j] = NULL;
			}
		}
	}while(numCharsEntered == 1 || !exitSig);

	return 0;
}