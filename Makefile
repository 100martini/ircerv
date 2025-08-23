NAME = webserv
CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98

PARSING_DIR = parsing
SRC_DIR = .
OBJ_DIR = obj

PARSING_SRCS = $(PARSING_DIR)/Config.cpp \
               $(PARSING_DIR)/Parser.cpp \
               $(PARSING_DIR)/Location.cpp \
               $(PARSING_DIR)/Utils.cpp

MAIN_SRCS = main.cpp

SRCS = $(MAIN_SRCS) $(PARSING_SRCS)

OBJS = $(SRCS:%.cpp=$(OBJ_DIR)/%.o)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

test: $(NAME)
	./$(NAME) webserv.conf

.PHONY: all clean fclean re test