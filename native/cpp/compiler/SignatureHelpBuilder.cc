#include "SignatureHelpBuilder.h"

#include "bscript/compiler/ast/Expression.h"
#include "bscript/compiler/ast/FunctionParameterDeclaration.h"
#include "bscript/compiler/ast/ModuleFunctionDeclaration.h"
#include "bscript/compiler/ast/UserFunction.h"
#include <stack>

using namespace Pol::Bscript::Compiler;
using namespace EscriptGrammar;

namespace VSCodeEscript::CompilerExt
{
SignatureHelpBuilder::SignatureHelpBuilder( CompilerWorkspace& workspace, const Position& position )
    : workspace( workspace ), position( position )
{
}

SignatureHelp make_signature_help(
    const std::string& function_name,
    std::vector<std::reference_wrapper<FunctionParameterDeclaration>> params,
    size_t active_parameter )
{
  bool added = false;
  std::string result = function_name + "(";
  std::vector<std::tuple<size_t, size_t>> parameters;
  parameters.reserve( params.size() );

  size_t current_position = result.size();
  for ( const auto& param_ref : params )
  {
    auto& param = param_ref.get();
    if ( added )
    {
      result += ", ";
      current_position += 2;
    }
    else
    {
      added = true;
    }
    result += param.name;

    parameters.push_back(
        std::make_tuple( current_position, current_position + param.name.size() ) );
    current_position += param.name.size();

    auto* default_value = param.default_value();
    if ( default_value )
    {
      auto default_description = default_value->describe();
      result += " := ";
      result += default_description;
      current_position += default_description.size() + 4;
    }
  }
  result += ")";
  return SignatureHelp{ result, std::move( parameters ), active_parameter };
}

std::optional<SignatureHelp> SignatureHelpBuilder::context()
{
  if ( workspace.source )
  {
    auto tokens = workspace.source->get_all_tokens();

    antlr4::Token* token;
    // Loop the source from the end to find the token containing our position
    for ( auto rit = tokens.rbegin(); rit != tokens.rend(); ++rit )
    {
      token = rit->get();
      Pol::Bscript::Compiler::Range range( token );

      // We found the token
      if ( range.contains( position ) )
      {
        // Holds parameter counts for nested function calls
        std::stack<size_t> param_counts;
        // Current parameter
        size_t current_param = 0;

        do
        {
          // A comma: increase parameter count
          if ( token->getType() == EscriptLexer::COMMA )
          {
            ++current_param;
          }
          // An open parenthesis: either...
          else if ( token->getType() == EscriptLexer::LPAREN )
          {
            // We have no inner function calls
            if ( param_counts.empty() )
            {
              // Get the identifier before this last open parenthesis
              if ( ++rit != tokens.rend() )
              {
                token = rit->get();
                if ( token->getType() == EscriptLexer::IDENTIFIER )
                {
                  auto function_name = token->getText();
                  if ( auto* module_function =
                           workspace.scope_tree.find_module_function( function_name ) )
                  {
                    return make_signature_help( module_function->name,
                                                module_function->parameters(), current_param );
                  }
                  else if ( auto* user_function =
                                workspace.scope_tree.find_user_function( function_name ) )
                  {
                    return make_signature_help( user_function->name, user_function->parameters(),
                                                current_param );
                  }
                }
              }

              // No need to continue trying anything else, as the best-effort above failed.
              return {};
            }
            else
            {
              current_param = param_counts.top();
              param_counts.pop();
            }
          }
          else if ( token->getType() == EscriptLexer::RPAREN )
          {
            param_counts.push( current_param );
            current_param = 0;
          }
          // FIXME improvement add other checks to bail-out fast, eg `if` cannot occur here.
          ++rit;
        } while ( rit != tokens.rend() && ( token = rit->get() ) );

        // No need to continue trying anything else, as the token was in range but everything failed
        return {};
      }
    }
  }
  return {};
}

}  // namespace VSCodeEscript::CompilerExt