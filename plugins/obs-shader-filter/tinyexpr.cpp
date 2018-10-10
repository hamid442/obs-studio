#include "tinyexpr.h"

#include <iostream>

using namespace std;

enum MathTokenType {
	number = 0,
	variable,
	function,
	infix,
	open,
	close,
	sepeartor
};

class Evaluator {
protected:
	string _expression;
	bool _evaluated;
	bool _tokenized;
	double _value;
	long long _maxLengthToken;
	unordered_map<string, MathToken*> _tokens;
	vector<MathToken*> _mathExpression;

	size_t _errIndex;
	size_t _errTokenIndex;
	string _error;

	bool addToken(const te_variable *var)
	{
		if (!var)
			return false;
		string token = var->name;
		MathToken *tok = nullptr;
		try {
			tok = _tokens.at(token);
			return false;
		} catch (out_of_range) {
			tok = new MathToken();
		}

		tok->token = token;
		if (var->type >= TE_FUNCTION0 && var->type <= TE_FUNCTION7) {
			tok->type = function;
			tok->arity = abs(var->type - TE_FUNCTION0);
			tok->function = var->address;
		} else if (var->type == TE_VARIABLE) {
			tok->type = variable;
			tok->bound = (const double*)var->address;
		}
		_tokens[token] = tok;
		return true;
	}
public:
	Evaluator()
	{

	}
	~Evaluator()
	{

	}

	vector<MathToken*> optimize(vector<MathToken*> mathExpression)
	{
		size_t i = 0;
		size_t j = 0;
		while (i < mathExpression.size()) {
			switch (mathExpression[i]->type) {
			case number:
				break;
			case variable:
				MathToken* tok = new MathToken();
				tok->type = number;
				tok->value = *mathExpression[i]->bound;
				mathExpression[i] = 
				break;
			case function:
				try {
					if (mathExpression.at(i + 1)->type != open) {
						_errTokenIndex = i;
						_error = "Expected '('";
						return;
					}
				} catch (out_of_range) {
					_errTokenIndex = i;
					_error = "Expected '('";
					return;
				}
				mathExpression[i]->value;
				break;
			case infix:
				break;
			case open:
				break;
			case close:
				break;
			case sepeartor:
				break;
			}
		}
	}

	void tokenize(string expression, bool cache = false)
	{
		size_t index = 0;
		size_t tokenSize;
		if (expression == _expression && _tokenized && cache) {
			return;
		}
		_tokenized = false;
		_expression = expression;
		for (size_t i = 0; i < _mathExpression.size(); i++) {
			MathToken* t = _mathExpression[i];
			if (t->type == number)
				delete t;
		}
		_mathExpression.clear();
		while (index < expression.length()) {
			MathToken* tok = nullptr;
			if (isspace(expression[index])) {
				index++;
				continue;
			} else if (isdigit(expression[index]) || expression[index] == '.') {
				/* Read as a number */
				char *end = nullptr;
				string expr = expression.substr(index);
				tok = new MathToken;
				tok->token = number;
				errno = 0;
				tok->value = strtod(expr.c_str(), &end);
				if (errno != 0) {
					_errIndex = index;
					string _error = "Could not be read as a number: " + expr;
					return;
				}
				_mathExpression.push_back(tok);
				int len = end - expr.c_str();
				index += len;
				continue;
			} else {
				for (tokenSize = _maxLengthToken; tokenSize > 0; tokenSize--) {
					try {
						string token = expression.substr(index, tokenSize);
						tok = _tokens.at(token);//_tokens[token];
						break;
					} catch (out_of_range) {
						continue;
					}
				}
				if (!tok) {
					_errIndex = index;
					string _error = "No tokens found in: " + expression.substr(index, _maxLengthToken);
					return;
				}
				_mathExpression.push_back(tok);
				index += tokenSize;
			}
		}
		_tokenized = true;
	}

	void clearTokens()
	{
		for (auto it : _tokens) {
			delete it.second;
		}
		_tokens.clear();
	}

	void processTokens(vector<te_variable> variables)
	{
		for (size_t i = 0; i < variables.size(); i++) {
			addToken(&variables[i]);
		}
	}

	void processTokens(const te_variable *variables, int var_count)
	{
		for (int i = 0; i < var_count; i++) {
			addToken(&variables[i]);
		}
	}

	vector<MathToken*> mathExpression()
	{
		return _mathExpression;
	}

	double calculate(vector<MathToken*> _mathExpression)
	{
		return 0;
	}

	double calculate()
	{
		return 0;
	}

	double evaluate(string expression, bool cache = false)
	{
		if (_evaluated && cache)
			return _value;

		_errIndex = 0;
		_error = "";
		tokenize(expression);
		if (!_error.empty())
			return 0;
		_value = calculate();
		_evaluated = true;
		return _value;
	}

	int errIndex()
	{
		return (int)_errIndex;
	}
};

static Evaluator tEval;
/* Parses the input expression, evaluates it, and frees it. */
/* Returns NaN on error. */
double te_interp(const char *expression, int *error);

/* Parses the input expression and binds variables. */
/* Returns NULL on error. */
te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error);

/* Evaluates the expression. */
double te_eval(const te_expr *n);

/* Prints debugging information on the syntax tree. */
void te_print(const te_expr *n);

/* Frees the expression. */
/* This is safe to call on NULL pointers. */
void te_free(te_expr *n);

double te_interp(const char *expression, int *error)
{
	double ret = tEval.evaluate(expression, false);
	*error = tEval.errIndex();
	return ret;
}

te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error)
{
	/*
	Add tokens
	*/
	tEval.clearTokens();
	//tEval.processTokens(, );
	tEval.processTokens(variables, var_count);

	tEval.tokenize(expression);
	*error = tEval.errIndex();
	vector<MathToken*> *n = new vector<MathToken*>();
	*n = tEval.mathExpression();
	return n;
}

double te_eval(const te_expr *n)
{
	return tEval.calculate(*n);
}

void te_print(const te_expr *n)
{
	for (size_t i = 0; i < n->size(); i++) {
		cout << "Param: " << n->at(i)->token << endl;
		cout << "Type: " << n->at(i)->type << endl;
	}
	return;
}

void te_free(te_expr *n)
{
	for (size_t i = 0; i < n->size(); i++) {
		MathToken* t = n->at(i);
		if(t->type == number)
			delete n->at(i);
	}
	delete n;
	return;
}
